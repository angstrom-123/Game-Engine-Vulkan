# TODO

## Categories

- Improvements 
- Features 
- Bugs

## Improvements

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

### Graphics settings config
- Yaml file to specify graphics options:
    - Bloom (on/off)
    - Shadow resolution (low/medium/high)
    - Anti-Aliasing (on/off)
    - VSync (on/off)

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
