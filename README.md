# Game Engine

This is a work-in-progress 3D game engine. My final goal is to have a starting 
point for creating games in the future and to learn about engines.

## Build and Run 

NOTE: Completely untested on ARM and on MacOS.

Bear in mind that this engine is built by me, for me. As such, the build is currently 
less-than-satisfactory. This may be addressed in a future version of the engine.

### Dependencies

Currently there is no auto-install of dependencies. To run this code, you will require:
- Vulkan SDK (verion >= 1.4)
- GLFW
These must be installed prior to building or running anything.
All other libraries are header-only and included directly in this repo.

### Linux 

This project is being developed on Linux and this is the intended platform for developers.
The build uses clang++ v18.1.3 by default, support for other C++ compilers is likely but not guaranteed.
To build the default example, enter the project's root and run:
```
make
```
Once built, you can run the example by running:
```
./bin/release/output
```
For this to work, you must be in the project's root directory as this is the working directory.

### Windows 

There is currently no supported way to build natively on Windows. It is possible to cross compile 
for Windows from Linux using MinGW. 

### Cross Compile Linux -> Windows

The build uses MinGW's g++ compiler (x86_64-w64-mingw32-g++), support for other compilers is likely but not guaranteed.
To build for Windows, run:
```
make TARGET_OS=windows
```
Once built, you can run this on Linux using wine:
```
wine ./winbin/release/output.exe
```
Alternatively, to run on Windows natively, clone this entire repository onto your Windows machine. 
Not everything from it is required, but there is currently no support for distribution builds so you will have to include the source. 
Next, copy across the compiled binary (output.exe) and the two dll's (libgcc_s_seh-1.dll, libstdc++-6.dll) from the `winbin/release` 
directory in your Linux environment to the same directory on your Windows machine (creating it if it doesn't exist).
Finally, you can run the code natively by connecting to the project root and running:
```
cmd.exe /k .\winbin\release\output.exe
```
Alternatively, you can run the executable using Windows explorer by moving the aforementioned binary and dll's 
to the project's root as this is the working directory.

## Libraries Used

- fkYAML
- GLFW
- GLM
- STB (image, image_resize2, truetype)
- VMA
- VkBootstrap
- Vulkan SDK

## Screenshots

<img width="1920" height="1080" alt="20260420_21h30m51s_grim" src="https://github.com/user-attachments/assets/68bc7ca8-1250-4e90-b6fc-7bdc6c0181b4" />
