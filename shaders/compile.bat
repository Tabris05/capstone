glslc model.vert -o model.vert.spv --target-env=vulkan1.4
glslc model.frag -o model.frag.spv --target-env=vulkan1.4
glslc skybox.vert -o skybox.vert.spv --target-env=vulkan1.4
glslc skybox.frag -o skybox.frag.spv --target-env=vulkan1.4
glslc srgb.comp -o srgb.comp.spv --target-env=vulkan1.4
glslc mip.comp -o mip.comp.spv --target-env=vulkan1.4
glslc cube.comp -o cube.comp.spv --target-env=vulkan1.4
pause