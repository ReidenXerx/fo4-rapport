# Autonomy Framework

NPC autonomy and player proposals for Fallout 4, built on [AAF](https://www.nexusmods.com/fallout4/mods/34260).

`DESIGN.md` is the specification. It outranks this file.

## What it targets

| | |
| --- | --- |
| Runtime | Fallout 4 **1.10.163** only (OG). Any other runtime is refused at load. |
| Script extender | F4SE 0.6.23 |
| Scene engine | AAF 1.7.4.1 |
| Base | [alandtse/CommonLibF4](https://github.com/alandtse/CommonLibF4), OG-only (`ENABLE_FALLOUT_NG/VR=OFF`) |

It replaces AAF Autonomy Enhanced and AAF Sex 'Em Up rather than running beside them.

## Building

Needs Visual Studio 2022 Build Tools (C++ workload) and [vcpkg](https://github.com/microsoft/vcpkg).

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config Release
```

The result is `AutonomyFramework.dll`, which belongs in `Data/F4SE/Plugins` alongside
`AutonomyFramework.ini` from `data/`.

## Where the numbers come from

Every claim about AAF's API in `DESIGN.md` was read out of the installed mod, not from memory:
`AAF_API.pex`'s own debug table for the function list, and AAF Sex 'Em Up's shipped sources for
the call shapes. See `docs/aaf-api.md`.
