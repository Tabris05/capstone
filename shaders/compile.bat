glslc prepass.vert -o prepass.vert.spv --target-env=vulkan1.4
glslc shadow.vert -o shadow.vert.spv --target-env=vulkan1.4
glslc model.vert -o model.vert.spv --target-env=vulkan1.4
glslc opaque.frag -o opaque.frag.spv --target-env=vulkan1.4
glslc blend.frag -o blend.frag.spv --target-env=vulkan1.4
glslc skybox.vert -o skybox.vert.spv --target-env=vulkan1.4
glslc skybox.frag -o skybox.frag.spv --target-env=vulkan1.4
glslc mip.comp -o mip.comp.spv --target-env=vulkan1.4
glslc srgbmip.comp -o srgbmip.comp.spv --target-env=vulkan1.4
glslc cube.comp -o cube.comp.spv --target-env=vulkan1.4
glslc cubemip.comp -o cubemip.comp.spv --target-env=vulkan1.4
glslc irradiance.comp -o irradiance.comp.spv --target-env=vulkan1.4
glslc radiance.comp -o radiance.comp.spv --target-env=vulkan1.4
glslc brdfintegral.comp -o brdfintegral.comp.spv --target-env=vulkan1.4
glslc postprocess.comp -o postprocess.comp.spv --target-env=vulkan1.4
pause