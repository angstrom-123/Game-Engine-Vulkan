# Game Engine

This is a starting point for a larger project that I am embarking upon to create
a high quality 3D engine and game for personal use.

## Libraries

- STB (image, image_resize2, truetype)
- VMA
- VkBootstrap
- GLM
- GLFW
- fkYAML
- Vulkan SDK

## Known Issues

- Extremely small memory leak after xcb_register_for_special_xge when first creating Swapchain. Known faulty interaction between GPU drivers, Vulkan, and GLFW - especially on Linux. This leak occurs at cleanup and is always measured at between 88 and 200 bytes.

## Screenshots

### Sponza shadowmap + random lights

<img width="1920" height="1080" alt="20260420_21h30m51s_grim" src="https://github.com/user-attachments/assets/68bc7ca8-1250-4e90-b6fc-7bdc6c0181b4" />
