The 3D model viewer I wrote in Vulkan for my Computer Science Capstone.
### Features
- PBR Material System
- Dynamic Direct Lighting
- Directional Shadowmapping
- Image-based Global Illumination
- Physically-Based Bloom
- Fast Approximate Antialiasing
- Orbital and Flycam Input Methods
- ACES, AgX, Khronos PBR Neutral, and Reinhard Tonemapping
- Asynchronous Asset Loading
- Full Scene Serialization to Custom File Format
- Efficient Bindless and GPU Driven Architecture
### Supported Formats
3D model files must be of the `.glTF` or `.glb` format and skyboxes must be equirectangular projections of the `.hdr` format. Full scenes must be of the custom `.mvs` format. `.mvs` files support Windows' `Open With` functionality.
### Controls
- **Orbital Mode:**
  - Hold `Left Click` when not hovering over a GUI element to pan the camera and use `Scroll Wheel` to change distance to the model.
- **Flycam Mode:**
  - Hold `Left Click` when not hovering over a GUI element to look around. Use`WSAD` to move forward/backward/left/right, `Space/LCTRL` to move up/down, and hold `LSHIFT` to increase speed.
- **User Interface:**
  - Most UI elements can be interacted with using only `Left Click`. However, `LCTRL + Left Click` can be used on any slider to manually type in a value.
### Dependencies:
- **[Dear ImGui](https://github.com/ocornut/imgui)** - UI elements
- **[fastgltf](https://github.com/spnda/fastgltf)** - 3D model loading
- **[GLFW](https://github.com/glfw/glfw)** - Window and input handling
- **[GLM](https://github.com/g-truc/glm)** - CPU-side linear algebra computations
- **[MikkTSpace](https://github.com/mmikk/MikkTSpace)** - Calculate vertex tangent vectors
- **[STB](https://github.com/nothings/stb)** - Image loading
- **[volk](https://github.com/zeux/volk)** - Dynamically load Vulkan functions
