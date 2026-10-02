# Third-party provenance

This project is distributed under GPL-2.0-or-later. `LICENSE` contains GPL v2; new project sources and the local libtesla integration permit version 2 or any later version.

| Component | Source / version | License and use |
|---|---|---|
| libtesla | https://github.com/WerWolv/libtesla at f766e9b607a05e9756843cbd62b3bfb98be1646c | GPL-2.0-or-later; complete header dependency, its embedded stb dependency and license are included under `third_party/libtesla` |
| stb_truetype | Supplied by that libtesla commit | MIT or public domain; upstream notices retained inside the header |
| libnx | Toolchain library 4.12.0-1, https://github.com/switchbrew/libnx | ISC; statically linked, license included under `third_party/libnx` |
| devkitPro tools | Versions and image digest in `toolchain.lock.json` | Used as build tools; not included in SD installation files |

The managed-layer stack shim and ARUID 0 input-permission call were adapted from libtesla's public wrappers; GPL terms and original project attribution are preserved here. The tiled pixel address calculation implements the framebuffer format documented by libnx. The sys-con HDLS implementation was inspected as an API/lifecycle reference; no sys-con source is compiled or bundled.

The accompanying `touchpad-1.1.0-beta.3-source.zip` supplies the corresponding project source, modified libtesla headers, build scripts, protocol and tests. Do not redistribute a modified binary without its matching source and notices.

Reference repositories, fixed source commits and archive hashes are recorded in `toolchain.lock.json`. Original reference-only downloads remain untouched in the user's downloads directory and are not part of this project's source archive.

The rc7 local libtesla integration adds checked focus restoration before the final clear, UI lifecycle tracing and a close guard for pending IPC. It releases the input mutex before callbacks, moves hide requests to the UI thread, fixes initialization/cleanup ordering of the polling thread and event, and avoids accessing an empty GUI stack. These changes remain under GPL-2.0-or-later.

The rc8 integration adds display stack membership tracking, an onShow pause barrier, menu layer removal before enable, and hide-source/layer diagnostics. The shared helper common/layer_stacks.hpp is included in the corresponding source.

The 1.1.0-beta.2 integration adds a local edge-swipe recognizer and reads the user's Ultrahand swipe setting. libultrahand was consulted only for gesture bounds/configuration; it is not compiled or bundled. Source URL and reference header digest are recorded in docs/SHOULDERS_SWIPE.md. The installed Ultrahand menu binary remains the user's existing file.
