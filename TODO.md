# TODO

## Rewrite Renderer
- Modular renderer with clearly defined and editable passes (except required passes)
- Cleaner internal set up 
- Support for screenspace rendering 
- Support for multiple viewports (multiple cameras)
- HDRI
- Custom output resolution in fullscreen
- Blurring of SSAO
- Deferred rendering with shader MSAA
- Bloom

## Fix Memory Leak
- Small bug between glfw, vulkan, and linux
- Caused by swapchain creation in vk bootstrap
- Constant leak amount at end of program (very minor)
