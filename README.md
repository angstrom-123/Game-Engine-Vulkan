# Game Engine

This is a work-in-progress 3D game engine. My final goal is to have a starting 
point for creating games in the future and to learn about engines.

## Build and Run 

NOTE: Completely untested on ARM and on MacOS.

Currently the build uses make and is only available on Linux, I have plans to switch 
to a better build system in the future.

### Dependencies

Make sure you have installed:
- Vulkan SDK (verion >= 1.4)
- GLFW

### Linux 

The build currently uses clang++ (version 18.1.3).
Run this from the project root:
```
make
```
Once built, you can run the example by running:
```
./bin/release/output
```

### Cross Compile Linux -> Windows

The build currently uses MinGW's g++ compiler (x86_64-w64-mingw32-g++).
Run this from the project root:
```
make TARGET_OS=windows
```
Once built, you can run this on Linux using wine:
```
wine ./winbin/release/output.exe
```
To run on windows:
- Clone this entire repository onto your Windows machine. (Currently no distribution build)
- Copy the compiled binary (output.exe) and the two dll's (libgcc_s_seh-1.dll and libstdc++-6.dll) from `winbin/release` 
in your Linux environment to the project root in your windows environment.
- You can then run by double-clicking the executable. 

## Libraries Used

- fkYAML
- GLFW
- GLM
- STB (image, truetype)
- VMA
- VkBootstrap
- Vulkan SDK

## Screenshots

<img width="1920" height="1080" alt="20260528_03h27m51s_grim" src="https://github.com/user-attachments/assets/90fcd43f-a70c-45c7-acd3-3b124c39e4f6" />
