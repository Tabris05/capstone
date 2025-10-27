#include "renderer.hpp"
#include <shared/vertex.h>
#include <shared/material.h>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <mikktspace/mikktspace.h>
#include <stb/stb_image.h>
#include <ranges>
#include <execution>
#include <tbrs/vk_util.hpp>

Renderer::Model Renderer::createModel(std::filesystem::path path) {
	const fastgltf::Extensions extensions =
		fastgltf::Extensions::KHR_materials_emissive_strength;

	fastgltf::Parser parser{ extensions };
	fastgltf::GltfDataBuffer data = std::move(fastgltf::GltfDataBuffer::FromPath(path).get());

	const fastgltf::Options options =
		fastgltf::Options::GenerateMeshIndices |
		fastgltf::Options::LoadExternalBuffers |
		fastgltf::Options::LoadExternalImages;

	const fastgltf::Asset asset{ std::move(parser.loadGltf(data, path.parent_path(), options).get()) };

	std::unordered_map<u32, b8> isSrgb;
	std::vector<u32> mipHandles;
	std::vector<Buffer> imageStagingBuffers;
	std::vector<Image> images;
	std::vector<u32> imageHandles;
	std::vector<u32> samplerHandles;
	std::vector<Material> materials;
	std::vector<Vertex> vertices;
	std::vector<u32> indices;
	std::vector<VkDrawIndexedIndirectCommand> opaqueDrawCmds;
	std::vector<VkDrawIndexedIndirectCommand> blendDrawCmds;
	std::vector<ImageHandle> descriptors;
	VkDescriptorPool pool = {};
	VkDescriptorSet set = {};

	AABB aabb;

	vkBeginCommandBuffer(m_transferCmdModelThread, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
	vkBeginCommandBuffer(m_computeCmdModelThread, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
	m_heap.bind(m_computeCmdModelThread);

	for(const fastgltf::Material& mat : asset.materials) {
		if(mat.pbrData.baseColorTexture.has_value()) {
			isSrgb[asset.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value()] = true;
		}
	}

	for(const auto& [idx, img] : std::views::enumerate(asset.images)) {
		i32 width;
		i32 height;
		const fastgltf::sources::Array& data = std::get<fastgltf::sources::Array>(img.data);
		stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.bytes.data()), data.bytes.size(), &width, &height, nullptr, STBI_rgb_alpha);

		u8 numMips = std::floor(std::log2(std::max(width, height))) + 1;

		Buffer stagingBuffer(m_ctx, width * height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		memcpy(stagingBuffer.map<void>(), pixels, width * height * 4);
		stbi_image_free(pixels);

		Image image(m_ctx, width, height, isSrgb[idx] ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, numMips);
		imageHandles.emplace_back(m_heap.allocImageHandle(ptr(image.viewCI()), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE));
		vkCmdInitializeColorImage(m_transferCmdModelThread, image.image());

		vkCmdCopyBufferToImage2(m_transferCmdModelThread, ptr(VkCopyBufferToImageInfo2{
			.srcBuffer = stagingBuffer.buffer(),
			.dstImage = image.image(),
			.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.regionCount = 1,
			.pRegions = ptr(VkBufferImageCopy2{
				.imageSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
				.imageExtent = { static_cast<u32>(width), static_cast<u32>(height), 1 }
			})
		}));

		VkImageViewCreateInfo ci{
			.image = image.image(),
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = VK_FORMAT_R8G8B8A8_UNORM,
			.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
		};

		u32 mip0Handle = m_heap.allocImageHandle(&ci, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		mipHandles.push_back(mip0Handle);

		vkCmdBindPipeline(m_computeCmdModelThread, VK_PIPELINE_BIND_POINT_COMPUTE, isSrgb[idx] ? m_srgbMipPipeline: m_mipPipeline);
		for(u8 i = 1; i < numMips; i++) {
			ci.subresourceRange.baseMipLevel = i;
			u32 curHandle = m_heap.allocImageHandle(&ci, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

			u32 handles[] = { mipHandles.back(), curHandle };
			vkCmdPushConstants(m_computeCmdModelThread, m_heap.layout(), VK_SHADER_STAGE_ALL, 0, sizeof(handles), handles);
			mipHandles.push_back(curHandle);

			vkCmdDispatch(m_computeCmdModelThread, (std::max(width >> i, 1) + 7) / 8, (std::max(height >> i, 1) + 7) / 8, 1);
			vkCmdBarrier(m_computeCmdModelThread, PipelineStage::ComputeWrite, PipelineStage::ComputeRead);
		}

		images.push_back(std::move(image));
		imageStagingBuffers.push_back(std::move(stagingBuffer));
	}

	vkEndCommandBuffer(m_computeCmdModelThread);

	for(const fastgltf::Sampler& s : asset.samplers) {
		u32 handle = m_heap.allocSamplerHandle(ptr(VkSamplerCreateInfo{
			.magFilter = m_filterMap.at(s.magFilter.value_or(fastgltf::Filter::Linear)),
			.minFilter = m_filterMap.at(s.minFilter.value_or(fastgltf::Filter::Linear)),
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = m_wrapMap.at(s.wrapS),
			.addressModeV = m_wrapMap.at(s.wrapT),
			.anisotropyEnable = true,
			.maxAnisotropy = 16,
			.maxLod = VK_LOD_CLAMP_NONE
		}));
		samplerHandles.push_back(handle);
	}

	for(const fastgltf::Texture tex : asset.textures) {
		descriptors.emplace_back(imageHandles[tex.imageIndex.value()], samplerHandles[tex.samplerIndex.value()]);
	}

	for(const fastgltf::Material& mat : asset.materials) {
		Material m = {
			.baseColor = glm::make_vec4(mat.pbrData.baseColorFactor.data()),
			.emissiveColor = glm::vec4(glm::make_vec3(mat.emissiveFactor.data()), mat.emissiveStrength),
			.metallic = mat.pbrData.metallicFactor,
			.roughness = mat.pbrData.roughnessFactor
		};

		if(mat.pbrData.baseColorTexture.has_value()) {
			isSrgb[asset.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value()] = true;
			m.albedoIndex = descriptors[mat.pbrData.baseColorTexture.value().textureIndex];
			m.texBitfield |= HAS_ALBEDO;
		}

		if(mat.normalTexture.has_value()) {
			m.normalIndex = descriptors[mat.normalTexture.value().textureIndex];
			m.texBitfield |= HAS_NORMAL;
		}

		if(mat.occlusionTexture.has_value()) {
			m.occlusionIndex = descriptors[mat.occlusionTexture.value().textureIndex];
			m.texBitfield |= HAS_OCCLUSION;
		}

		if(mat.pbrData.metallicRoughnessTexture.has_value()) {
			m.metallicRoughnessIndex = descriptors[mat.pbrData.metallicRoughnessTexture.value().textureIndex];
			m.texBitfield |= HAS_METALLIC_ROUGHNESS;
		}

		if(mat.emissiveTexture.has_value()) {
			isSrgb[asset.textures[mat.emissiveTexture.value().textureIndex].imageIndex.value()] = true;
			m.emissiveIndex = descriptors[mat.emissiveTexture.value().textureIndex];
			m.texBitfield |= HAS_EMISSIVE;
		}

		materials.push_back(m);
	}

	const u64 materialBufferByteSize = materials.size() * sizeof(Material);
	Buffer stagingMaterialBuffer(m_ctx, materialBufferByteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	Buffer materialBuffer(m_ctx, materialBufferByteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	memcpy(stagingMaterialBuffer.map<void>(), materials.data(), materialBufferByteSize);

	vkCmdCopyBuffer(m_transferCmdModelThread, stagingMaterialBuffer.buffer(), materialBuffer.buffer(), 1, ptr(VkBufferCopy{ .size = materialBufferByteSize }));
	u64 numVertices = 0;
	u64 numIndices = 0;
	u64 numPrimitives = 0;
	for(const fastgltf::Mesh curMesh : asset.meshes) {
		for(u64 i = 0; i < curMesh.primitives.size(); i++) {
			numVertices += asset.accessors[curMesh.primitives[i].findAttribute("POSITION")->accessorIndex].count;
			numIndices += asset.accessors[curMesh.primitives[i].indicesAccessor.value()].count;
			numPrimitives++;
		}
	}

	vertices.reserve(numVertices);
	indices.reserve(numIndices);
	opaqueDrawCmds.reserve(numPrimitives);

	auto processNode = [&](this auto& self, u64 index, glm::mat4 transform) -> void {
		const fastgltf::Node& curNode = asset.nodes[index];
		transform *= std::visit(fastgltf::visitor{
			[](fastgltf::math::fmat4x4 matrix) {
				return glm::make_mat4(matrix.data());
			},
			[](fastgltf::TRS trs) {
				return glm::translate(glm::mat4{ 1.0f }, glm::make_vec3(trs.translation.data()))
				* glm::toMat4(glm::make_quat(trs.rotation.value_ptr()))
				* glm::scale(glm::mat4{ 1.0f }, glm::make_vec3(trs.scale.data()));
			}
		}, curNode.transform);

		if(curNode.meshIndex.has_value()) {
			const glm::mat3 normalTransform{ glm::transpose(glm::inverse(transform)) };
			const fastgltf::Mesh& curMesh = asset.meshes[curNode.meshIndex.value()];
			for(const fastgltf::Primitive& curPrimitive : curMesh.primitives) {
				const u64 oldVerticesSize = vertices.size();
				const u64 oldIndicesSize = indices.size();

				const fastgltf::Accessor& indexAccessor = asset.accessors[curPrimitive.indicesAccessor.value()];
				fastgltf::iterateAccessor<u32>(asset, indexAccessor, [&indices](u32 index) {
					indices.emplace_back(index);
				});

				const fastgltf::Accessor& positionAccessor = asset.accessors[curPrimitive.findAttribute("POSITION")->accessorIndex];
				fastgltf::iterateAccessor<glm::vec3>(asset, positionAccessor, [&vertices, &aabb, transform](glm::vec3 pos) {
					glm::vec3 vertex = glm::vec3(transform * glm::vec4(pos, 1.0f));
					aabb.min = glm::min(aabb.min, vertex);
					aabb.max = glm::max(aabb.max, vertex);
					vertices.emplace_back(vertex);
				});

				const fastgltf::Accessor& normalAccessor = asset.accessors[curPrimitive.findAttribute("NORMAL")->accessorIndex];
				fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, normalAccessor, [&vertices, oldVerticesSize, normalTransform](glm::vec3 normal, u64 index) {
					vertices[index + oldVerticesSize].normal = glm::normalize(normalTransform * normal);
				});

				const fastgltf::Attribute* uvAccessorIndex;
				if((uvAccessorIndex = curPrimitive.findAttribute("TEXCOORD_0")) != curPrimitive.attributes.cend()) {
					const fastgltf::Accessor& uvAccessor = asset.accessors[uvAccessorIndex->accessorIndex];
					fastgltf::iterateAccessorWithIndex<glm::vec2>(asset, uvAccessor, [&vertices, oldVerticesSize](glm::vec2 uv, u64 index) {
						vertices[index + oldVerticesSize].uv = uv;
					});
				}

				const fastgltf::Attribute* tangentAccessorIndex;
				if((tangentAccessorIndex = curPrimitive.findAttribute("TANGENT")) != curPrimitive.attributes.cend()) {
					const fastgltf::Accessor& tangentAccessor = asset.accessors[tangentAccessorIndex->accessorIndex];
					fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, tangentAccessor, [&vertices, oldVerticesSize, normalTransform](glm::vec4 tangent, u64 index) {
						vertices[index + oldVerticesSize].tangent = glm::vec4(glm::normalize(glm::vec3(normalTransform * glm::vec4(glm::vec3(tangent), 0.0f))), tangent.w);
					});
				}
				else if(uvAccessorIndex != curPrimitive.attributes.cend()) {
					struct UsrPtr {
						const u64 vertexOffset;
						const u64 indexOffset;
						std::vector<Vertex>& vertices;
						std::vector<u32>& indices;
					} usrPtr{ oldVerticesSize, oldIndicesSize, vertices, indices };

					SMikkTSpaceInterface interface {
						[](const SMikkTSpaceContext* ctx) -> i32 {
							UsrPtr* data = static_cast<UsrPtr*>(ctx->m_pUserData);
								return (data->indices.size() - data->indexOffset) / 3;
							},
							[](const SMikkTSpaceContext*, const i32) -> i32 {
								return 3;
							},
							[](const SMikkTSpaceContext* ctx, f32 outPos[], const i32 face, const i32 vert) {
								UsrPtr* data = static_cast<UsrPtr*>(ctx->m_pUserData);
								memcpy(outPos, &data->vertices[data->indices[data->indexOffset + face * 3 + vert]].position, sizeof(glm::vec3));
							},
							[](const SMikkTSpaceContext* ctx, f32 outNorm[], const i32 face, const i32 vert) {
								UsrPtr* data = static_cast<UsrPtr*>(ctx->m_pUserData);
								memcpy(outNorm, &data->vertices[data->vertexOffset + data->indices[data->indexOffset + face * 3 + vert]].normal, sizeof(glm::vec3));
							},
							[](const SMikkTSpaceContext* ctx, f32 outUV[], const i32 face, const i32 vert) {
								UsrPtr* data = static_cast<UsrPtr*>(ctx->m_pUserData);
								memcpy(outUV, &data->vertices[data->vertexOffset + data->indices[data->indexOffset + face * 3 + vert]].uv, sizeof(glm::vec2));
							},
							[](const SMikkTSpaceContext* ctx, const f32 inTangent[], const f32 sign, const i32 face, const i32 vert) {
								UsrPtr* data = static_cast<UsrPtr*>(ctx->m_pUserData);
								u64 vertexIndex = data->vertexOffset + data->indices[data->indexOffset + face * 3 + vert];
								memcpy(&data->vertices[vertexIndex].tangent, inTangent, sizeof(glm::vec3));
								data->vertices[vertexIndex].tangent.w = sign;
							}
					};
					SMikkTSpaceContext ctx{ &interface, &usrPtr };
					genTangSpaceDefault(&ctx);
				}

				VkDrawIndexedIndirectCommand cmd = { indices.size() - oldIndicesSize, 1, oldIndicesSize, oldVerticesSize, curPrimitive.materialIndex.value() };

				if(asset.materials[curPrimitive.materialIndex.value()].alphaMode == fastgltf::AlphaMode::Blend) {
					blendDrawCmds.push_back(cmd);
				}
				else {
					opaqueDrawCmds.push_back(cmd);
				}
			}
		}
		for(u64 i : curNode.children) {
			self(i, transform);
		}
	};

	for(u64 i : asset.scenes[asset.defaultScene.value_or(0)].nodeIndices) {
		glm::mat4 transform(1.0f);
		processNode(i, transform);
	}

	const glm::vec3 center = (aabb.max + aabb.min) / 2.0f;
	const glm::vec3 size = aabb.max - aabb.min;
	const f32 scale = 1.0f / std::max(size.x, std::max(size.y, size.z));

	const glm::mat4 baseTransform = glm::scale(glm::mat4(1.0f), glm::vec3(scale)) * glm::translate(glm::mat4(1.0f), -center);

	const u64 vertexBufferByteSize = vertices.size() * sizeof(Vertex);
	const u64 indexBufferByteSize = indices.size() * sizeof(u32);
	const u64 opaqueIndirectBufferByteSize = opaqueDrawCmds.size() * sizeof(VkDrawIndexedIndirectCommand);
	const u64 blendIndirectBufferByteSize = blendDrawCmds.size() * sizeof(VkDrawIndexedIndirectCommand);
	const u64 indirectBufferByteSize = opaqueIndirectBufferByteSize + blendIndirectBufferByteSize;
	Buffer stagingVertexBuffer(m_ctx, vertexBufferByteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	Buffer stagingIndexBuffer(m_ctx, indexBufferByteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	Buffer stagingIndirectBuffer(m_ctx, indirectBufferByteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	Buffer vertexBuffer(m_ctx, vertexBufferByteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	Buffer indexBuffer(m_ctx, indexBufferByteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	Buffer indirectBuffer(m_ctx, indirectBufferByteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	memcpy(stagingVertexBuffer.map<void>(), vertices.data(), vertexBufferByteSize);
	memcpy(stagingIndexBuffer.map<void>(), indices.data(), indexBufferByteSize);
	memcpy(stagingIndirectBuffer.map<void>(), opaqueDrawCmds.data(), opaqueIndirectBufferByteSize);
	memcpy(reinterpret_cast<char*>(stagingIndirectBuffer.map<void>()) + opaqueIndirectBufferByteSize, blendDrawCmds.data(), blendIndirectBufferByteSize);
	
	vkCmdCopyBuffer(m_transferCmdModelThread, stagingVertexBuffer.buffer(), vertexBuffer.buffer(), 1, ptr(VkBufferCopy{ .size = vertexBufferByteSize }));
	vkCmdCopyBuffer(m_transferCmdModelThread, stagingIndexBuffer.buffer(), indexBuffer.buffer(), 1, ptr(VkBufferCopy{ .size = indexBufferByteSize }));
	vkCmdCopyBuffer(m_transferCmdModelThread, stagingIndirectBuffer.buffer(), indirectBuffer.buffer(), 1, ptr(VkBufferCopy{ .size = indirectBufferByteSize }));
	vkEndCommandBuffer(m_transferCmdModelThread);

	m_dmaTransferLock.lock();
	vkQueueSubmit2(m_ctx.transferQueue(), 1, ptr(VkSubmitInfo2{
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{.commandBuffer = m_transferCmdModelThread }),
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = ptr(VkSemaphoreSubmitInfo{
			.semaphore = m_modelSem.semaphore(),
			.value = m_modelSem.advance(),
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		})
	}), nullptr);
	m_dmaTransferLock.unlock();

	m_asyncComputeLock.lock();
	vkQueueSubmit2(m_ctx.computeQueue(), 1, ptr(VkSubmitInfo2{
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = ptr(VkSemaphoreSubmitInfo{
			.semaphore = m_modelSem.semaphore(),
			.value = m_modelSem.submittedValue(),
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		}),
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{.commandBuffer = m_computeCmdModelThread }),
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = ptr(VkSemaphoreSubmitInfo{
			.semaphore = m_modelSem.semaphore(),
			.value = m_modelSem.advance(),
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		}),
	}), nullptr);
	m_asyncComputeLock.unlock();

	m_modelSem.wait();

	vkResetCommandPool(m_ctx.device(), m_transferPoolModelThread, 0);

	for(u32 handle : mipHandles) {
		m_heap.freeImageHandle(handle);
	}

	vkResetCommandPool(m_ctx.device(), m_computePoolModelThread, 0);

	Model model;
	model.heap = &m_heap;
	model.images = std::move(images);
	model.imageHandles = std::move(imageHandles);
	model.samplerHandles = std::move(samplerHandles);
	model.materialBuffer = std::move(materialBuffer);
	model.vertexBuffer = std::move(vertexBuffer);
	model.indexBuffer = std::move(indexBuffer);
	model.indirectBuffer = std::move(indirectBuffer);
	model.baseTransform = baseTransform;
	model.aabb = aabb;
	model.numOpaqueDrawCommands = opaqueDrawCmds.size();
	model.numBlendDrawCommands = blendDrawCmds.size();

	return model;
}