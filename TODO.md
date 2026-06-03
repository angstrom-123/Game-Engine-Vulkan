# TODO

## Categories

- Improvements 
- Features 
- Bugs

## Improvements

### VKB 
- Replace vk bootstrap instance, device, physical device, swapchain creation

### Embedding
- Embed some default resources as headers 
- Could use for engine font, etc.

### Anti-Aliasing
- SMAA
- Also add AA to bloom bright spot image

### Shadow
- Soft shadows

### GLTF instead of OBJ
- Replace existing OBJ loader with a GLTF loader 
- Add support for emissive materials

## Features 

### IBL 
- Image based lighting 
- Cubemap image imports or otherwise 

### Add Fonts To Manifest File 
- Allow each scene to create its own font resources
- Allow font selection during text creation

## Bugs

### Fix Memory Leak
- Small bug between glfw, vulkan, and linux
- Caused by swapchain creation in vk bootstrap
- Constant leak amount at end of program (very minor)
