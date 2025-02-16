glslc model.vert -o model.vert.spv --target-env=vulkan1.4
glslc model.frag -o model.frag.spv --target-env=vulkan1.4
glslc srgb.comp -o srgb.comp.spv --target-env=vulkan1.4
glslc mip.comp -o mip.comp.spv --target-env=vulkan1.4
pause