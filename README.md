# RT-Thread SDK for BK7252

The repository contains a SDK for Beken BK7252, an ARM-based Wi-fi + BLE combo chip. Originally mirrored from [Chinese gitee](https://gitee.com/iot_camp/rtt_bk7252_sdk), it is maintained by me as a part of open firmware project for A9 mini camera.

# Changes

- SCons build system was migrated to Python 3 (mostly done by aider and GPT-4o),
- code was patched to be compiled by **Arm GNU Toolchain 14.2.Rel1 (Build arm-14.52)) 14.2.1 20241119**, i.e. the newest Arm GNU Toolchain at that time,
- newlib and pthread implementation was removed, as it caused conflicts with newlib from the toolchain.

# Notes

My inspiration was [Daniel's repository](https://github.com/daniel-dona/beken7252-opencam), but please note that for some reason the SDK originally mirrored by me differs slightly from Daniel's.

If you have any issues with my variation of SDK, you may want to try [this fork](https://github.com/Apache02/beken7252-opencam) instead. It was updated to a newer toolchain independently from my work.

The code is a mess. I tried to configure it using [RT-Thread Env v1.5.x](https://github.com/RT-Thread/env), but I had a few issues related both to my setup (Python 2 and tqdm dependencies) and low quality of the code base (unmaintained Kconfig scripts). In the end my suggested workflow is to edit manually *rtconfig.h* or remove unused code. I was not able to configure it in a way that would allow to make out-of-tree builds.

The code was NOT tested against a real target. I will do it when I have time and proper tools for the job - I am gonna use A9 mini camera PCB with BK7252 in QFN48 as a devboard.

*beken378* folder contains both unmaintained code (written probably as a part of FreeRTOS port) and code essential for using chip functionality, e.g. HAL. In *beken378/ip* you can find *libip_7221u.a* and *libip_7221u_sta.a*, which are implementation of Wi-fi transceiver API. *beken378/driver* folder contains drivers for peripherals conforming to a common driver model defined in *beken378/driver/common*. HAL from *rt-thread/components/drivers* rely on them through RT-Thread API calls, so I would not call them drivers per se, rather userspace-equivalent API implementation.

For now the code cannot be successfully linked due to insufficient memory space, but you can easily solve it by removal of components unused by your project. Alternatively, you can try to comment them out (if possible) in *rtconfig.h*.