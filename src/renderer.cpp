#include "renderer.hpp"
#include <tbrs/vk_util.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_vulkan.h>
#include <imgui/imgui_spinner.h>
#include <numbers>
#include <fstream>

void Renderer::run() {
	f32 thisFrame = 0.0f;
	f32 lastFrame = 0.0f;
	while(!glfwWindowShouldClose(m_window)) {
		glfwPollEvents();

		if(const ImGuiIO& io = ImGui::GetIO(); !io.WantCaptureMouse && !io.WantCaptureKeyboard) {
			handleInput(thisFrame - lastFrame);
		}
		render(thisFrame);

		lastFrame = thisFrame;
		thisFrame = glfwGetTime();
	}
}

void Renderer::handleInput(f32 deltaTime) {
	if(m_camIdx == 1) {
		if(glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) {
			m_position += m_speed * m_rotation * glm::vec3(deltaTime);
		}
		if(glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) {
			m_position -= m_speed * m_rotation * glm::vec3(deltaTime);
		}
		if(glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) {
			m_position += m_speed * glm::normalize(glm::cross(m_rotation, glm::vec3(0.0f, 1.0f, 0.0f))) * glm::vec3(deltaTime);
		}
		if(glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) {
			m_position -= m_speed * glm::normalize(glm::cross(m_rotation, glm::vec3(0.0f, 1.0f, 0.0f))) * glm::vec3(deltaTime);
		}
		if(glfwGetKey(m_window, GLFW_KEY_SPACE) == GLFW_PRESS) {
			m_position += m_speed * glm::vec3(0.0f, 1.0f, 0.0f) * glm::vec3(deltaTime);
		}
		if(glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
			m_position -= m_speed * glm::vec3(0.0f, 1.0f, 0.0f) * glm::vec3(deltaTime);
		}
		if(glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
			m_speed = 5.76;
		}
		else if(glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_RELEASE) {
			m_speed = 1.44f;
		}

		if(glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
			glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

			if(m_firstClick) {
				m_firstClick = false;
				glfwSetCursorPos(m_window, m_width / 2, m_height / 2);
			}

			f64 mouseX, mouseY;
			glfwGetCursorPos(m_window, &mouseX, &mouseY);

			f32 rotX = m_sensitivity * 200.0f * static_cast<f32>(mouseX - (m_width / 2)) / m_width;
			f32 rotY = m_sensitivity * 200.0f * static_cast<f32>(mouseY - (m_height / 2)) / m_height;

			f32 oldPitch = glm::degrees(std::asin(m_rotation.y));
			f32 newPitch = glm::clamp(oldPitch - rotY, -m_maxPitch, m_maxPitch);
			f32 deltaPitch = newPitch - oldPitch;

			m_rotation = glm::rotate(m_rotation, glm::radians(-rotX), glm::vec3(0.0f, 1.0f, 0.0f));
			m_rotation = glm::rotate(m_rotation, glm::radians(deltaPitch), glm::normalize(glm::cross(m_rotation, glm::vec3(0.0f, 1.0f, 0.0f))));
			glfwSetCursorPos(m_window, m_width / 2, m_height / 2);
		}
		else if(glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE) {
			glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			m_firstClick = true;
		}
		m_scroll = 0.0f;
	}
	else {
		if(glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS || m_scroll != 0.0f) {
			glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

			if(m_firstClick) {
				m_firstClick = false;
				glfwSetCursorPos(m_window, m_width / 2.0, m_height / 2.0);
			}

			f64 mouseX, mouseY;
			glfwGetCursorPos(m_window, &mouseX, &mouseY);

			f32 distance = glm::length(m_position);
			f32 yaw = glm::degrees(std::atan2(m_position.x, m_position.z));
			f32 pitch = glm::degrees(std::asin(m_position.y / distance));

			f32 rotX = m_sensitivity * 200.0f * static_cast<f32>(mouseX - (m_width / 2)) / m_width;
			f32 rotY = m_sensitivity * 200.0f * static_cast<f32>(mouseY - (m_height / 2)) / m_height;

			yaw -= rotX;
			pitch = glm::clamp(pitch + rotY, -m_maxPitch, m_maxPitch);

			distance = std::max(distance - m_scroll * m_scrollSensitivity * 0.2f, std::numeric_limits<f32>::epsilon());
			m_scroll = 0.0;

			m_position.x = distance * std::cos(glm::radians(pitch)) * std::sin(glm::radians(yaw));
			m_position.y = distance * std::sin(glm::radians(pitch));
			m_position.z = distance * std::cos(glm::radians(pitch)) * std::cos(glm::radians(yaw));

			m_rotation = glm::normalize(-m_position);

			glfwSetCursorPos(m_window, m_width / 2, m_height / 2);
		}
		else if(glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE) {
			glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			m_firstClick = true;
		}
	}
}

void Renderer::render(f32 thisFrame) {
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ImGui::BeginDisabled(m_savingScene);
	// camera Menu
	{
		ImGui::Begin("Camera");
		ImGui::Text("Controls");
		if(ImGui::Combo("Input Method", &m_camIdx, ptr({ "Orbital", "Flycam" }), 2)) {
			if(m_camIdx == 0) {
				m_rotation = glm::normalize(-m_position);
			}
		}

		ImGui::SliderFloat("Look Sensitivity", &m_sensitivity, 0.1f, 1.0f, nullptr, ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Scroll Sensitivity", &m_scrollSensitivity, 0.1f, 1.0f, nullptr, ImGuiSliderFlags_AlwaysClamp);

		ImGui::NewLine();
		ImGui::Text("Display");
		ImGui::SliderFloat("Field Of View", &m_fov, 60.0f, 120.0f, nullptr, ImGuiSliderFlags_AlwaysClamp);
		ImGui::Combo("Color Grading", &m_colorIdx, ptr({ "ACES", "AgX", "Khronos PBR Neutral", "Reinhard" }), 4);

		ImGui::End();
	}

	// lighting menu
	{
		ImGui::Begin("Lighting");
		ImGui::ColorPicker3("Light Color", &m_lightColor.x, ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_DisplayRGB);
		ImGui::SliderFloat("Light Intensity", &m_lightColor.w, 0.0f, 10.0f, nullptr, ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Light Pitch", &m_lightPitch, -0.5f, 0.5f, nullptr, ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Light Yaw", &m_lightYaw, -0.5f, 0.5f, nullptr, ImGuiSliderFlags_AlwaysClamp);
		ImGui::End();

		f32 pitch = m_lightPitch * 2.0f * std::numbers::pi_v<f32>;
		f32 yaw = m_lightYaw * 2.0f * std::numbers::pi_v<f32>;

		m_lightAngle.x = std::cos(pitch) * std::sin(yaw);
		m_lightAngle.y = std::sin(pitch);
		m_lightAngle.z = std::cos(pitch) * std::cos(yaw);
	}

	// model menu
	{
		ImGui::Begin("Model");
		ImGui::SliderFloat("Model Scale", &m_modelScale, 0.1f, 5.0f, nullptr, ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Model Pitch", &m_modelPitch, -0.5f, 0.5f, nullptr, ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Model Yaw", &m_modelYaw, -0.5f, 0.5f, nullptr, ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Model Roll", &m_modelRoll, -0.5f, 0.5f, nullptr, ImGuiSliderFlags_AlwaysClamp);
		ImGui::End();
	}

	// options menu
	{
		ImGui::Begin("Options");
		ImGui::Combo("Antialiasing", &m_AAIdx, ptr({ "None" }), 1);

		b8 vsyncBefore = m_vsync;
		ImGui::Checkbox("Enable Vsync", &m_vsync);
		if(m_vsync != vsyncBefore) {
			m_swapchainDirty = true;
		}

		ImGui::NewLine();
		ImGui::Text("%.0f FPS, %.2fms", m_fpsLastSecond, 1000.0 / m_fpsLastSecond);
		
		ImGui::End();
	}

	// scene menu
	{
		ImGui::Begin("Scene");

		ImGui::BeginDisabled(m_loadingModel || m_loadingScene);
		if(ImGui::Button("Load Model")) {
			m_loadingModel = true;
			m_modelFuture = std::async(std::launch::async, [this] {
				nfdu8char_t* outPath;

				nfdresult_t result = NFD_OpenDialogU8_With(&outPath, ptr(nfdopendialogu8args_t{
					.filterList = ptr({ nfdu8filteritem_t{ "glTF Binary", "glb" }, nfdu8filteritem_t{ "glTF Seperate", "gltf" } }),
					.filterCount = 2,
					.parentWindow = m_nativeHandle
				}));

				Model model;

				if(result == NFD_OKAY) {
					m_modelPath = outPath;
					model = createModel(outPath);
					NFD_FreePathU8(outPath);
				}

				return model;
			});
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		
		ImGui::BeginDisabled(m_loadingSkybox || m_loadingScene);
		if(ImGui::Button("Load Environment Map")) {
			m_loadingSkybox = true;
			m_skyboxFuture = std::async(std::launch::async, [this] {
				nfdu8char_t* outPath;

				nfdresult_t result = NFD_OpenDialogU8_With(&outPath, ptr(nfdopendialogu8args_t{
					.filterList = ptr(nfdu8filteritem_t{ "Environment Map", "hdr" }),
					.filterCount = 1,
					.parentWindow = m_nativeHandle
				}));

				Skybox skybox;
				if(result == NFD_OKAY) {
					m_skyboxPath = outPath;
					skybox = createSkybox(outPath);
					NFD_FreePathU8(outPath);
				}

				return skybox;
			});
		}
		ImGui::EndDisabled();

		ImGui::BeginDisabled(m_loadingModel || m_loadingSkybox || m_savingScene || m_loadingScene);
		if(ImGui::Button("Save Scene")) {
			m_savingScene = true;
			m_savingSceneFuture = std::async(std::launch::async, [this] {
				nfdu8char_t* outPath;

				nfdresult_t result = NFD_SaveDialogU8_With(&outPath, ptr(nfdsavedialogu8args_t{
					.filterList = ptr(nfdu8filteritem_t{ "vkModelViewer Scene", "mvs" }),
					.filterCount = 1,
					.parentWindow = m_nativeHandle
				}));

				if(result != NFD_OKAY) {
					return;
				}

				std::ofstream file(outPath, std::ios::binary);
				NFD_FreePathU8(outPath);

				auto writeVariable = [&file](const auto& var) {
					file.write(reinterpret_cast<const char*>(&var), sizeof(var));
				};

				writeVariable(m_scroll);
				writeVariable(m_position);
				writeVariable(m_rotation);
				writeVariable(m_vsync);
				writeVariable(m_camIdx);
				writeVariable(m_colorIdx);
				writeVariable(m_AAIdx);
				writeVariable(m_fov);
				writeVariable(m_sensitivity);
				writeVariable(m_scrollSensitivity);
				writeVariable(m_lightPitch);
				writeVariable(m_lightYaw);
				writeVariable(m_modelPitch);
				writeVariable(m_modelYaw);
				writeVariable(m_modelRoll);
				writeVariable(m_modelScale);
				writeVariable(m_lightAngle);
				writeVariable(m_lightColor);
				writeVariable(m_modelPath.size());
				file.write(m_modelPath.data(), m_modelPath.size());
				writeVariable(m_skyboxPath.size());
				file.write(m_skyboxPath.data(), m_skyboxPath.size());
			});
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(m_loadingModel || m_loadingSkybox);
		if(ImGui::Button("Load Scene")) {
			m_loadingScene = true;
			m_loadingSceneFuture = std::async(std::launch::async, [this] {
				nfdu8char_t* outPath;

				nfdresult_t result = NFD_OpenDialogU8_With(&outPath, ptr(nfdopendialogu8args_t{
					.filterList = ptr(nfdu8filteritem_t{ "vkModelViewer Scene", "mvs" }),
					.filterCount = 1,
					.parentWindow = m_nativeHandle
				}));

				if(result != NFD_OKAY) {
					return;
				}
					std::ifstream file(outPath, std::ios::binary);
					NFD_FreePathU8(outPath);

				auto readVariable = [&file](auto& var) {
					file.read(reinterpret_cast<char*>(&var), sizeof(var));
				};

				readVariable(m_scroll);
				readVariable(m_position);
				readVariable(m_rotation);
				readVariable(m_vsync);
				readVariable(m_camIdx);
				readVariable(m_colorIdx);
				readVariable(m_AAIdx);
				readVariable(m_fov);
				readVariable(m_sensitivity);
				readVariable(m_scrollSensitivity);
				readVariable(m_lightPitch);
				readVariable(m_lightYaw);
				readVariable(m_modelPitch);
				readVariable(m_modelYaw);
				readVariable(m_modelRoll);
				readVariable(m_modelScale);
				readVariable(m_lightAngle);
				readVariable(m_lightColor);

				u64 size = 0;
				readVariable(size);
				m_modelPath.resize(size);
				file.read(m_modelPath.data(), size);

				readVariable(size);
				m_skyboxPath.resize(size);
				file.read(m_skyboxPath.data(), size);
			});
		}
		ImGui::EndDisabled();

		ImGui::End();
	}
	ImGui::EndDisabled();

	if(m_loadingModel || m_loadingSkybox || m_loadingScene) {
		ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("##loadingWindow", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
		);
		ImGui::PushFont(m_largeFont);
		ImGui::Text("Loading...");
		ImGui::PopFont();
		ImGui::SameLine();
		ImGui::Spinner("##spinner", 18.0f, 6.0f, ImVec4(0.92f, 0.18f, 0.29f, 1.00f));
		ImGui::End();
	}
	else if(m_savingScene) {
		ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("##savingWindow", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
		);
		ImGui::PushFont(m_largeFont);
		ImGui::Text("Saving...");
		ImGui::PopFont();
		ImGui::SameLine();
		ImGui::Spinner("##spinner", 18.0f, 6.0f, ImVec4(0.92f, 0.18f, 0.29f, 1.00f));
		ImGui::End();
	}
	ImGui::Render();

	f32 orthoSize = std::sqrt(2.0f) / 2.0f * m_modelScale;
	glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(m_modelScale))
		* glm::rotate(glm::mat4(1.0f), m_modelPitch * 2.0f * std::numbers::pi_v<f32>, glm::vec3(1.0f, 0.0f, 0.0f))
		* glm::rotate(glm::mat4(1.0f), m_modelYaw * 2.0f * std::numbers::pi_v<f32>, glm::vec3(0.0f, 1.0f, 0.0f))
		* glm::rotate(glm::mat4(1.0f), m_modelRoll * 2.0f * std::numbers::pi_v<f32>, glm::vec3(0.0f, 0.0f, 1.0f))
		* m_model.baseTransform;
	glm::mat4 view = glm::lookAt(m_position, m_camIdx == 1 ? m_position + m_rotation : glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 projection = perspective(glm::radians(m_fov / 2.0f), static_cast<f32>(m_width) / static_cast<f32>(m_height), 0.1f);
	glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), m_lightAngle, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 lightProjection = ortho(orthoSize, -orthoSize, -orthoSize, orthoSize, -orthoSize, orthoSize);
	glm::mat4 camMatrixNoTranslation = projection * glm::mat4(glm::mat3(view));

	PushConstants pushConstants = {
		m_oitBuffer.devicePtr,
		m_model.vertexBuffer.devicePtr,
		m_model.materialBuffer.devicePtr,
		m_poissonDiskBuffer.devicePtr,
		projection * view,
		lightProjection * lightView,
		model,
		m_lightColor,
		m_position,
		m_lightAngle,
		m_width
	};

	auto& frameData = m_perFrameData[m_frameIndex];
	vkWaitForFences(m_device, 1, &frameData.fence, true, std::numeric_limits<u64>::max());

	while(!frameData.deletionQueue.empty()) {
		frameData.deletionQueue.front()();
		frameData.deletionQueue.pop();
	}

	u32 imageIndex;
	VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, std::numeric_limits<u64>::max(), frameData.acquireSem, nullptr, &imageIndex);
	if(result == VK_ERROR_OUT_OF_DATE_KHR) {
		recreateSwapchain();
		return;
	}

	vkResetFences(m_device, 1, &frameData.fence);
	vkResetCommandPool(m_device, frameData.cmdPool, 0);

	vkBeginCommandBuffer(frameData.cmdBuffer, ptr(VkCommandBufferBeginInfo{ .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));

	vkCmdSetViewport(frameData.cmdBuffer, 0, 1, ptr(VkViewport{ 0.0f, 0.0f, static_cast<f32>(m_shadowMapSize), static_cast<f32>(m_shadowMapSize), 0.0f, 1.0f }));
	vkCmdSetScissor(frameData.cmdBuffer, 0, 1, ptr(VkRect2D{ { 0, 0 }, { static_cast<u32>(m_shadowMapSize), static_cast<u32>(m_shadowMapSize) } }));

	// shadow pass
	{
		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
				.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				.image = m_shadowMap.image,
				.subresourceRange = depthSubresourceRange()
			})
		}));

		vkCmdBeginRendering(frameData.cmdBuffer, ptr(VkRenderingInfo{
			.renderArea = { 0, 0, { static_cast<u32>(m_shadowMapSize), static_cast<u32>(m_shadowMapSize) } },
			.layerCount = 1,
			.pDepthAttachment = ptr(VkRenderingAttachmentInfo{
				.imageView = m_shadowMap.view,
				.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = { 0.0f }
			})
		}));

		if(m_model.numOpaqueDrawCommands > 0) {
			vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
			vkCmdBindIndexBuffer(frameData.cmdBuffer, m_model.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdPushConstants(frameData.cmdBuffer, m_modelPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pushConstants);
			vkCmdDrawIndexedIndirect(frameData.cmdBuffer, m_model.indirectBuffer.buffer, 0, m_model.numOpaqueDrawCommands, sizeof(VkDrawIndexedIndirectCommand));
		}

		vkCmdEndRendering(frameData.cmdBuffer);
	}

	// depth pre-pass
	{
		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 2,
			.pImageMemoryBarriers = ptr({
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.image = m_shadowMap.image,
					.subresourceRange = depthSubresourceRange(),
				},
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
					.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
					.image = m_depthTarget.image,
					.subresourceRange = depthSubresourceRange()
				}
			})
		}));

		vkCmdSetViewport(frameData.cmdBuffer, 0, 1, ptr(VkViewport{ 0.0f, 0.0f, static_cast<f32>(m_width), static_cast<f32>(m_height), 0.0f, 1.0f }));
		vkCmdSetScissor(frameData.cmdBuffer, 0, 1, ptr(VkRect2D{ { 0, 0 }, { static_cast<u32>(m_width), static_cast<u32>(m_height) } }));

		vkCmdBeginRendering(frameData.cmdBuffer, ptr(VkRenderingInfo{
			.renderArea = { 0, 0, { static_cast<u32>(m_width), static_cast<u32>(m_height) } },
			.layerCount = 1,
			.pDepthAttachment = ptr(VkRenderingAttachmentInfo{
				.imageView = m_depthTarget.view,
				.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = { 0.0f }
			})
		}));

		if(m_model.numOpaqueDrawCommands > 0) {
			vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_prepassPipeline);
			vkCmdDrawIndexedIndirect(frameData.cmdBuffer, m_model.indirectBuffer.buffer, 0, m_model.numOpaqueDrawCommands, sizeof(VkDrawIndexedIndirectCommand));
		}

		vkCmdEndRendering(frameData.cmdBuffer);
	}

	// opaque color pass
	{
		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 2,
			.pImageMemoryBarriers = ptr({
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					.image = m_colorTarget.image,
					.subresourceRange = colorSubresourceRange()
				},
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
					.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
					.image = m_depthTarget.image,
					.subresourceRange = depthSubresourceRange()
				}
			})
		}));

		vkCmdBeginRendering(frameData.cmdBuffer, ptr(VkRenderingInfo{
			.renderArea = { 0, 0, { static_cast<u32>(m_width), static_cast<u32>(m_height) } },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = ptr(VkRenderingAttachmentInfo{
				.imageView = m_colorTarget.view,
				.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = { 0.0f, 0.0f, 0.0f, 1.0f }
			}),
			.pDepthAttachment = ptr(VkRenderingAttachmentInfo{
				.imageView = m_depthTarget.view,
				.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			})
		}));

		if(m_model.numOpaqueDrawCommands > 0) {
			vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_opaquePipeline);
			vkCmdBindDescriptorSets(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_modelPipelineLayout, 0, 1, &m_model.texSet, 0, nullptr);
			vkCmdPushDescriptorSet(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_modelPipelineLayout, 1, 4, ptr({
				VkWriteDescriptorSet{
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_skybox.irradianceMap.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				},
				VkWriteDescriptorSet{
					.dstBinding = 1,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_skybox.radianceMap.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				},
				VkWriteDescriptorSet{
					.dstBinding = 2,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_brdfIntegralTex.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				},
				VkWriteDescriptorSet{
					.dstBinding = 3,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_shadowSampler,
						.imageView = m_shadowMap.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				}
			}));
			vkCmdDrawIndexedIndirect(frameData.cmdBuffer, m_model.indirectBuffer.buffer, 0, m_model.numOpaqueDrawCommands, sizeof(VkDrawIndexedIndirectCommand));
		}

		if(m_skybox.environmentMap.image != VkImage{}) {
			vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipeline);
			vkCmdPushDescriptorSet(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = ptr(VkDescriptorImageInfo{
					.sampler = m_skyboxSampler,
					.imageView = m_skybox.environmentMap.view,
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				})
				}));
			vkCmdPushConstants(frameData.cmdBuffer, m_skyboxPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &camMatrixNoTranslation);
			vkCmdDraw(frameData.cmdBuffer, 36, 1, 0, 0);
		}

		vkCmdEndRendering(frameData.cmdBuffer);
	}

	// transparent color pass
	{
		if(m_model.numBlendDrawCommands > 0) {
			vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
				.bufferMemoryBarrierCount = 1,
				.pBufferMemoryBarriers = ptr(VkBufferMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
					.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
					.buffer = m_oitBuffer.buffer,
					.size = VK_WHOLE_SIZE
				})
			}));

			vkCmdFillBuffer(frameData.cmdBuffer, m_oitBuffer.buffer, 0, VK_WHOLE_SIZE, 0);

			vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
				.bufferMemoryBarrierCount = 1,
				.pBufferMemoryBarriers = ptr(VkBufferMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
					.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					.buffer = m_oitBuffer.buffer,
					.size = VK_WHOLE_SIZE
				})
			}));

			vkCmdBeginRendering(frameData.cmdBuffer, ptr(VkRenderingInfo{
				.renderArea = { 0, 0, { static_cast<u32>(m_width), static_cast<u32>(m_height) } },
				.layerCount = 1,
				.pDepthAttachment = ptr(VkRenderingAttachmentInfo{
					.imageView = m_depthTarget.view,
					.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
					.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
					.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				})
			}));

			vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_blendPipeline);

			vkCmdBindDescriptorSets(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_modelPipelineLayout, 0, 1, &m_model.texSet, 0, nullptr);
			vkCmdPushDescriptorSet(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_modelPipelineLayout, 1, 4, ptr({
				VkWriteDescriptorSet{
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_skybox.irradianceMap.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				},
				VkWriteDescriptorSet{
					.dstBinding = 1,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_skybox.radianceMap.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				},
				VkWriteDescriptorSet{
					.dstBinding = 2,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_brdfIntegralTex.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				},
				VkWriteDescriptorSet{
					.dstBinding = 3,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_shadowSampler,
						.imageView = m_shadowMap.view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					})
				}
			}));
			vkCmdPushConstants(frameData.cmdBuffer, m_modelPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pushConstants);

			vkCmdDrawIndexedIndirect(frameData.cmdBuffer, m_model.indirectBuffer.buffer, m_model.numOpaqueDrawCommands * sizeof(VkDrawIndexedIndirectCommand), m_model.numBlendDrawCommands, sizeof(VkDrawIndexedIndirectCommand));

			vkCmdEndRendering(frameData.cmdBuffer);
		}
	}

	// ui pass
	{
		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 2,
			.pImageMemoryBarriers = ptr({
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					.image = m_uiTarget.image,
					.subresourceRange = colorSubresourceRange()
				},
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.image = m_swapchainImages[imageIndex],
					.subresourceRange = colorSubresourceRange()
				}
			})
		}));

		vkCmdBeginRendering(frameData.cmdBuffer, ptr(VkRenderingInfo{
			.renderArea = { 0, 0, { static_cast<u32>(m_width), static_cast<u32>(m_height) } },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = ptr(VkRenderingAttachmentInfo{
				.imageView = m_uiTarget.view,
				.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.clearValue = { 0.0f, 0.0f, 0.0f, 0.0f }
			})
		}));

		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frameData.cmdBuffer);

		vkCmdEndRendering(frameData.cmdBuffer);
	}

	// composite transparency pass
	{
		if(m_model.numBlendDrawCommands > 0) {
			vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
				.bufferMemoryBarrierCount = 1,
				.pBufferMemoryBarriers = ptr(VkBufferMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
					.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					.buffer = m_oitBuffer.buffer,
					.size = VK_WHOLE_SIZE
				}),
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.image = m_colorTarget.image,
					.subresourceRange = colorSubresourceRange()
				}),
			}));

			vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_transparencyCompositePipeline);
			vkCmdPushConstants(frameData.cmdBuffer, m_transparencyCompositePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VkDeviceAddress), &m_oitBuffer.devicePtr);
			vkCmdPushDescriptorSet(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_transparencyCompositePipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.pImageInfo = ptr({
					VkDescriptorImageInfo{
						.imageView = m_colorTarget.view,
						.imageLayout = VK_IMAGE_LAYOUT_GENERAL
					}
				})
			}));
			vkCmdDispatch(frameData.cmdBuffer, (m_width + 7) / 8, (m_height + 7) / 8, 1);

			vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = ptr({
					VkImageMemoryBarrier2{
						.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
						.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
						.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
						.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
						.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						.image = m_colorTarget.image,
						.subresourceRange = colorSubresourceRange()
					},
				})
			}));
		}
	}

	// bloom pass
	{
		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = m_model.numBlendDrawCommands > 0 ? 1u : 2u,
			.pImageMemoryBarriers = ptr({
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
					.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.image = m_bloomTarget.image,
					.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
				},
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
					.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					.image = m_colorTarget.image,
					.subresourceRange = colorSubresourceRange()
				}
			})
		}));

		vkCmdCopyImage2(frameData.cmdBuffer, ptr(VkCopyImageInfo2{
			.srcImage = m_colorTarget.image,
			.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.dstImage = m_bloomTarget.image,
			.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.regionCount = 1,
			.pRegions = ptr(VkImageCopy2{
				.srcSubresource = colorSubresourceLayers(),
				.srcOffset = { 0, 0, 0 },
				.dstSubresource = colorSubresourceLayers(),
				.dstOffset = { 0, 0, 0 },
				.extent = { static_cast<u32>(m_width), static_cast<u32>(m_height), 1 }
			})
		}));

		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 3,
			.pImageMemoryBarriers = ptr({
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
					.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.image = m_bloomTarget.image,
					.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
				},
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.image = m_bloomTarget.image,
					.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_REMAINING_MIP_LEVELS, 0, 1 }
				},
				VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.image = m_colorTarget.image,
					.subresourceRange = colorSubresourceRange()
				}
			})
		}));

		vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_bloomDownsamplePipeline);
		for(u32 i = 1; i < m_bloomMips.size(); i++) {
			vkCmdPushDescriptorSet(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_bloomPipelineLayout, 0, 2, ptr({
				VkWriteDescriptorSet{
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_bloomMips[i - 1].first,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					}),
				},
				VkWriteDescriptorSet{
					.dstBinding = 1,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.imageView = m_bloomMips[i].first,
						.imageLayout = VK_IMAGE_LAYOUT_GENERAL
					})
				},
			}));
			vkCmdPushConstants(frameData.cmdBuffer, m_bloomPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(u32), ptr(i - 1));
			vkCmdDispatch(frameData.cmdBuffer, (m_bloomMips[i].second.x + 7) / 8, (m_bloomMips[i].second.y + 7) / 8, 1);

			vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
					.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.image = m_bloomTarget.image,
					.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1 }
				})
			}));
		}

		vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_bloomUpsamplePipeline);
		for(u32 i = m_bloomMips.size() - 1; i >= 1; i--) {
			vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.image = m_bloomTarget.image,
					.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1 }
				})
			}));

			vkCmdPushDescriptorSet(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_bloomPipelineLayout, 0, 2, ptr({
				VkWriteDescriptorSet{
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.sampler = m_skyboxSampler,
						.imageView = m_bloomMips[i].first,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					}),
				},
				VkWriteDescriptorSet{
					.dstBinding = 1,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = ptr(VkDescriptorImageInfo{
						.imageView = m_bloomMips[i - 1].first,
						.imageLayout = VK_IMAGE_LAYOUT_GENERAL
					})
				},
			}));
			vkCmdPushConstants(frameData.cmdBuffer, m_bloomPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(u32), &i);
			vkCmdDispatch(frameData.cmdBuffer, (m_bloomMips[i - 1].second.x + 7) / 8, (m_bloomMips[i - 1].second.y + 7) / 8, 1);

			vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
					.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.image = m_bloomTarget.image,
					.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1 }
				})
			}));
		}

		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.image = m_bloomTarget.image,
				.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
			})
		}));
	}

	// post-processing pass
	{
		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.image = m_uiTarget.image,
				.subresourceRange = colorSubresourceRange()
			})
		}));

		vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_postprocessingPipeline);
		vkCmdPushConstants(frameData.cmdBuffer, m_postprocessingPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(u32), &m_colorIdx);
		vkCmdPushDescriptorSet(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_postprocessingPipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
			.descriptorCount = 3,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = ptr({
				VkDescriptorImageInfo{
					.imageView = m_colorTarget.view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				},
				VkDescriptorImageInfo{
					.imageView = m_bloomMips.front().first,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				},
				VkDescriptorImageInfo{
					.imageView = m_swapchainImageViews[imageIndex],
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				}
			})
		}));

		vkCmdDispatch(frameData.cmdBuffer, (m_width + 7) / 8, (m_height + 7) / 8, 1);

		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.image = m_swapchainImages[imageIndex],
				.subresourceRange = colorSubresourceRange()
			})
		}));
	}

	// composite ui pass
	{
		
		vkCmdBindPipeline(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_uiCompositePipeline);
		vkCmdPushDescriptorSet(frameData.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_twoImagePipelineLayout, 0, 1, ptr(VkWriteDescriptorSet{
			.descriptorCount = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = ptr({
				VkDescriptorImageInfo{
					.imageView = m_swapchainImageViews[imageIndex],
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				},
				VkDescriptorImageInfo{
					.imageView = m_uiTarget.view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				}
			})
		}));
		vkCmdDispatch(frameData.cmdBuffer, (m_width + 7) / 8, (m_height + 7) / 8, 1);

		vkCmdPipelineBarrier2(frameData.cmdBuffer, ptr(VkDependencyInfo{
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = ptr(VkImageMemoryBarrier2{
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				.image = m_swapchainImages[imageIndex],
				.subresourceRange = colorSubresourceRange()
			})
		}));
	}

	vkEndCommandBuffer(frameData.cmdBuffer);

	vkQueueSubmit2(m_graphicsQueue, 1, ptr(VkSubmitInfo2{
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = ptr(VkSemaphoreSubmitInfo{
			.semaphore = frameData.acquireSem,
			.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
		}),
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = ptr(VkCommandBufferSubmitInfo{.commandBuffer = frameData.cmdBuffer }),
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = ptr(VkSemaphoreSubmitInfo{
			.semaphore = frameData.presentSem,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		}),
	}), frameData.fence);

	result = vkQueuePresentKHR(m_graphicsQueue, ptr(VkPresentInfoKHR{
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &frameData.presentSem,
		.swapchainCount = 1,
		.pSwapchains = &m_swapchain,
		.pImageIndices = &imageIndex
	}));

	if(result != VK_SUCCESS || m_swapchainDirty) {
		recreateSwapchain();
	}

	if(m_loadingModel && m_modelFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		m_loadingModel = false;
		Model tmp = m_modelFuture.get();
		if(tmp.vertexBuffer.buffer != VkBuffer{}) {
			std::swap(tmp, m_model);
			frameData.deletionQueue.push([this, tmp] { destroyModel(tmp); });
		}
	}

	if(m_loadingSkybox && m_skyboxFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		m_loadingSkybox = false;
		Skybox tmp = m_skyboxFuture.get();
		if(tmp.environmentMap.image != VkImage{}) {
			std::swap(tmp, m_skybox);
			frameData.deletionQueue.push([this, tmp] { destroySkybox(tmp); });
		}
	}

	if(m_savingScene && m_savingSceneFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		m_savingScene = false;
	}

	if(m_loadingScene && m_loadingSceneFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		m_loadingScene = false;

		m_loadingModel = true;
		m_modelFuture = std::async(std::launch::async, [this] {
			Model model;
			std::filesystem::path path(m_modelPath);

			if(std::filesystem::exists(path)) {
				model = createModel(path);
			}

			return model;
		});

		m_loadingSkybox = true;
		m_skyboxFuture = std::async(std::launch::async, [this] {
			Skybox skybox;
			std::filesystem::path path(m_skyboxPath);

			if(std::filesystem::exists(path)) {
				skybox = createSkybox(path);
			}

			return skybox;
		});
	}

	m_frameIndex = (m_frameIndex + 1) % m_framesInFlight;

	m_framesThisSecond++;
	if(thisFrame - m_lastSecond >= 1.0f) {
		m_fpsLastSecond = m_framesThisSecond / (thisFrame - m_lastSecond);
		m_lastSecond = thisFrame;
		m_framesThisSecond = 0;
	}
}