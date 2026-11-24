# RocketWeb kernel

The physics kernel behind the `rocketSim*` Lua functions (rocket soccer),
copied unchanged from the desktop Neon tree (`vendor/rocketweb`), where both
the desktop client and the server link it. The client steps the same kernel
as the server, so it must stay identical to the server's copy: update it only
from the desktop tree, all files together.

| Path | What it is |
| --- | --- |
| `generated/reference.c`, `reference.h`, `arena_data.h` | The kernel and arena, translated from car-soccer.com's WebAssembly with WABT wasm2c 1.0.41. |
| `Kernel.c`, `Kernel.h` | Neon's host for it (`nrk_*`). |
| `runtime/` | The wasm2c runtime. |

## Rights

`NOTICE.txt` is Neon's notice, unchanged: the generated kernel and arena data
come from car-soccer.com and their rights remain with their owners; including
them does not place them under this project's license. The project owner
chose to include them, as the desktop Neon tree does, and accepts that risk.
The wasm2c runtime is Apache License 2.0 (`runtime/LICENSE`).

## Build

`CMakeLists.txt` uses the desktop premake settings (`NEON_WASM_UNSHARED_ONLY`,
no mmap, bounds-checked memory, so no signal handler) and builds without
fused multiply-add or fast-math, so results match the server's x86 build.
