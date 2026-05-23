# TODO

## Graphics settings config
- Yaml file to specify graphics options:
    - Bloom (on/off)
    - Shadow resolution (low/medium/high)
    - Anti-Aliasing (on/off)
    - VSync (on/off)

## GLTF instead of OBJ
- Replace existing OBJ loader with a GLTF loader 
- Add support for emissive materials

## IBL 
- Image based lighting 
- Cubemap image imports or otherwise 

## Shadow Improvements
- Frustum culling shadowcasting light depth renders 
- Cascades
- Soft shadows

## TAA
- Current anti-aliasing has very poor quality
- TAA is expensive but far better

## Add Fonts To Manifest File 
- Allow each scene to create its own font resources
- Allow font selection during text creation

## Fix Memory Leak
- Small bug between glfw, vulkan, and linux
- Caused by swapchain creation in vk bootstrap
- Constant leak amount at end of program (very minor)
