# HeadshotsKillF4SE

F4SE plugin for **Fallout 4** that adds caliber-aware headshot rules, optional instakill behavior, helmet knockoff with Havok impulses, melee knockoff, player feedback (audio/visual), and an in-game configuration menu.

Repository: [github.com/DCCStudios/F4-HeadshotsKillF4SE](https://github.com/DCCStudios/F4-HeadshotsKillF4SE)

## Requirements

- **Fallout 4** with [F4SE](https://f4se.silverlock.org/) matching your game build (OG / Next Gen per your CommonLibF4 target).
- **Visual Studio 2022** (or compatible MSVC) with C++23 support.
- **CMake** 3.21 or newer.
- **[vcpkg](https://github.com/microsoft/vcpkg)** — set `VCPKG_ROOT` so CMake can use the vcpkg toolchain (see `vcpkg.json`).

## Repository layout (important)

`CMakeLists.txt` pulls **CommonLibF4** from a sibling path:

```
YourDevTree/
  PluginTemplate/
    CommonLibF4/
      CommonLibF4/     ← add_subdirectory target
      ...
  HeadshotsKillF4SE/   ← this repository (clone here)
```

If you only clone this repo, either place it next to your existing `PluginTemplate/CommonLibF4` tree or adjust the `add_subdirectory` path in `CMakeLists.txt`.

## Build

```powershell
cd HeadshotsKillF4SE
cmake --preset msvc-release
cmake --build build/release --config Release
```

Built artifacts are written under **`Compile/F4SE/Plugins/`** (DLL, default INI copy, and plugin subfolder for JSON).

Optional: define environment variable **`Fallout4Path`** to your game root and configure with **`-DCOPY_BUILD=ON`** to copy the DLL, PDB, INI, and `ammo_calibers.json` into `Data/F4SE/Plugins/` after each build (see `CMakeLists.txt`).

## Install (players / testers)

Copy into the game’s **`Data\F4SE\Plugins\`** folder:

| Item | Location |
|------|----------|
| `HeadshotsKillF4SE.dll` | `Data\F4SE\Plugins\` |
| `HeadshotsKillF4SE.ini` | `Data\F4SE\Plugins\` |
| `ammo_calibers.json` | `Data\F4SE\Plugins\HeadshotsKillF4SE\` |

Edit the INI or use the in-game menu; the UI expects **`F4SEMenuFramework.dll`** under `Data\F4SE\Plugins\` (F4SE Menu Framework).

## Configuration

- **`HeadshotsKillF4SE.ini`** — main settings (chances, helmet knockoff, player rules, feedback, etc.).
- **`config/ammo_calibers.json`** — deployed beside the DLL as `HeadshotsKillF4SE/ammo_calibers.json`; caliber classification and overrides.

## Logging

With debug logging enabled, the plugin writes to **`Documents\My Games\Fallout4\F4SE\HeadshotsKillF4SE.log`**.

## License

Package metadata in `vcpkg.json` lists **MIT**; add a root `LICENSE` file if you want that spelled out explicitly on GitHub.

## Tools

The **`tools/`** directory contains optional Python helpers for data extraction (developer workflow, not required at runtime).
