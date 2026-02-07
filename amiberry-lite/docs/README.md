# Optimized Amiga emulator for multiple platforms

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/X8X4FHDY4)

<a rel="me" href="https://mastodon.social/@midwan">Follow me on Mastodon!</a>

[![C/C++ CI](https://github.com/BlitterStudio/amiberry-lite/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/BlitterStudio/amiberry/actions/workflows/c-cpp.yml)

[![Discord](https://img.shields.io/badge/My-Discord-%235865F2.svg)](https://discord.gg/wWndKTGpGV)

![Amiberry logo](https://i2.wp.com/blitterstudio.com/wp-content/uploads/2020/01/Logo-v3-1.png?resize=768%2C543&ssl=1)

Amiberry-Lite is an optimized Amiga emulator for ARM (32bit and 64bit) and RISC-V platforms.

The core emulation comes from [WinUAE](https://www.winuae.net), and even the main GUI is designed to look similar to that.
It includes JIT for ARM support, to get high-performance results on CPU-intensive emulated environments, like desktop applications. On top of that, there are some unique features developed specifically for Amiberry, such as the WHDLoad booter, support for RetroArch controller mapping, and several more.

There are currently two Amiberry editions available: **Amiberry and Amiberry-Lite** - [see here](https://github.com/BlitterStudio/amiberry/wiki/First-Installation) to see which is the best pick for your needs.

## Getting Amiberry-Lite

### Linux

Amiberry-Lite is available as a DEB package for ARM32 and ARM64 (aarch64) Linux platforms. You can download the latest version from the [Releases](https://github.com/BlitterStudio/amiberry-lite/releases) area.
Users of macOS and x86_64 Linux are better off using the full [Amiberry](https://github.com/BlitterStudio/amiberry) version, instead.

For DEB packages:

      sudo apt update && sudo apt install ./amiberry-lite_5.8.0_arm64.deb

Or just use the graphical front-end your distro provides (double-clicking on the DEB package should do that)

### Distro package management

Some distros (like AmiKit, RetroPie, DietPi, Pimiga and others) already include Amiberry either pre-installed, or through their package management systems. Please follow the methods provided in those distros for a smoother experience, and refer to their owners for support during this process.

## Compile from source

Alternatively, you can [compile the latest version of Amiberry from source](https://github.com/BlitterStudio/amiberry/wiki/Compile-from-source).

For more documentation subjects, please check the [Wiki page](https://github.com/BlitterStudio/amiberry/wiki)

### Supported by

[![JetBrains logo.](https://resources.jetbrains.com/storage/products/company/brand/logos/jetbrains.svg)](https://jb.gg/OpenSourceSupport)
