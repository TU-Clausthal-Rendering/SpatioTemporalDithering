![](docs/images/teaser.png)

# Spatio-Temporal Dithering for Order-Independent Transparency on Ray Tracing Hardware

Teaser:

[![YouTube](http://i.ytimg.com/vi/07vLuLH_1wU/hqdefault.jpg)](https://www.youtube.com/watch?v=07vLuLH_1wU)

## Contents:

* [Demo User Interface](#demo-user-interface)
* [Falcor Prerequisites](#falcor-prerequisites)
* [Building Falcor](#building-falcor)

## Demo User Interface

This project was implemented in NVIDIAs Falcor rendering framework.

You can download the executable demo from the [Releases Page](https://github.com/kopaka1822/Falcor/releases/tag/Ray-SD), or build the project by following the instructions in [Building Falcor](#building-falcor).

After downloading the demo from the releases page, you can execute it with the RunFalcor.bat file. In the Demo, you can configure the renderer after expanding the **DitherVBuffer** tab. In the **DitherVBuffer** tab you can select the specific transparency technique in the **Dither** dropdown:
* Disabled: Everything will be interpreted as opaque
* STD 3x3: Our STD from the paper, with 3x3 dither matrices
* DitherTemporalAA: Unreal Engine's DTAA
* RussianRoulette: Russian Roulette renderer from the paper
* HashGrid: (not in the paper) This renderer tries to pin the noise to the surface of the objects, instead of pinning it to screen space. This works somewhat well for white and blue noise, but produces extreme moiree patterns for regular patterns like bayer dithering matrices.
* FractalDithering: (not in the paper) This technique also tries to pin a texture to the surface of the objects, but uses a recurring fractal dithering pattern to do so. Unfortunately, scaling this dithering pattern to subpixel also produces extreme moiree patterns. The technique itself comes from this [Youtube](https://www.youtube.com/watch?v=HPqGaIMVuLs&pp=ygURZnJhY3RhbCBkaXRoZXJpbmc%3D) video.
* STD 2x2: STD, but with 2x2 dither matrices (using all 24 permutations of 2x2 matrices)
* Periodic: Tries to utilize the uniform distribution modulo one, to select the optimal temporal sequence. Unfortunately, this flickers a lot.
* SpatioTemporalBlueNoise: Implementation based on Wolfe et al. \[WMAR22\] (Screen-Space noise)
* SurfaceSpatioTemporalBlueNoise: Same as above, but the texture is being attached to the surfaces as in the HashGrid technique above.
* BlueNoise3D: Screen-space blue noise, where the third axis is represented by time.

=> For the baseline, set the Hybrid Threshold slider to 0.0

In the correction dropdown you can enable or disable the DLSS correction.

You can navigate the camera with WASD and dragging the mouse for rotation.
Hold shift for more camera speed
QE for camera up and down
Space to pause the animation

## Falcor Prerequisites
- Windows 10 version 20H2 (October 2020 Update) or newer, OS build revision .789 or newer
- Visual Studio 2022
- [Windows 10 SDK (10.0.19041.0) for Windows 10, version 2004](https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk/)
- A GPU which supports DirectX Raytracing, such as the NVIDIA Titan V or GeForce RTX
- NVIDIA driver 466.11 or newer

Optional:
- Windows 10 Graphics Tools. To run DirectX 12 applications with the debug layer enabled, you must install this. There are two ways to install it:
    - Click the Windows button and type `Optional Features`, in the window that opens click `Add a feature` and select `Graphics Tools`.
    - Download an offline package from [here](https://docs.microsoft.com/en-us/windows-hardware/test/hlk/windows-hardware-lab-kit#supplemental-content-for-graphics-media-and-mean-time-between-failures-mtbf-tests). Choose a ZIP file that matches the OS version you are using (not the SDK version used for building Falcor). The ZIP includes a document which explains how to install the graphics tools.
- NVAPI, CUDA, OptiX

## Building Falcor
Falcor uses the [CMake](https://cmake.org) build system. Additional information on how to use Falcor with CMake is available in the [CMake](docs/development/cmake.md) development documentation page.

### Visual Studio
If you are working with Visual Studio 2022, you can setup a native Visual Studio solution by running `setup_vs2022.bat` after cloning this repository. The solution files are written to `build/windows-vs2022` and the binary output is located in `build/windows-vs2022/bin`.
