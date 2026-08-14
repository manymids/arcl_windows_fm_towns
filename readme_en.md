# ARCL Windows FM TOWNS — Windows FM TOWNS Emulator / MCP Server

[日本語版](readme.md)

This project is a Windows frontend for the FM TOWNS MX emulator based on [TOWNSEMU (Tsugaru)](https://github.com/captainys/TOWNSEMU). In addition to normal interactive use through a small GUI, it can run as a stdio server for the [Model Context Protocol (MCP)](https://modelcontextprotocol.io/). An MCP client can capture the display and audio, send input, control media, inspect debugging information, and manage save states.

This repository contains no ROM, Towns OS, game software, CD, floppy-disk, or hard-disk images. Use only files that you have obtained and are entitled to use.

## Requirements

- Windows 10 or Windows 11, 64-bit
- An FM TOWNS II MX ROM set compatible with UNZ
- For source builds: a Visual Studio MSVC x64 development environment and CMake 3.20 or newer

The frontend and the documented build procedure target Windows / MSVC x64 only.

## Required files

### ROM

FM TOWNS ROMs are copyrighted firmware of Fujitsu and other rights holders. Obtain ROMs from hardware you own, or compatible ROMs that you are entitled to use, then place them here:

```text
TOWNSEMU/
  roms/
    ROM_MX/
      FMT_DIC.ROM
      FMT_DOS.ROM
      FMT_F20.ROM
      FMT_FNT.ROM
      FMT_SYS.ROM
```

`TOWNSEMU\\roms\\` is not tracked by Git. Create it after a fresh clone if necessary:

```powershell
New-Item -ItemType Directory -Force TOWNSEMU\roms\ROM_MX
```

Do not commit or redistribute ROMs, Towns OS, game software, or disk images with this project.

### CD / FD / HDD images

Store the media you intend to boot anywhere on the host. TOWNSEMU supports `.ISO`, `.CUE`, and `.MDS` CD images. For games with CD audio tracks, `.MDS` / `.MDF` is recommended because interpretation of `PREGAP` / `POSTGAP` in `.CUE` files differs between image-creation tools.

The examples below use `C:\FM_TOWNS\game.cue`. Quote paths containing spaces as individual command arguments.

## Build

1. Install Visual Studio with the Desktop development with C++ workload and CMake.
2. At the repository root, open an x64 Native Tools Command Prompt, or otherwise enable the MSVC x64 environment.
3. Run:

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S TOWNSEMU\src -B arcl_windows_fmtowns\build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build arcl_windows_fmtowns\build --target arcl_windows_fmtowns
```

The executable is written to `arcl_windows_fmtowns\build\bin\arcl_windows_fmtowns.exe`. To rebuild from a clean CMake configuration, remove `arcl_windows_fmtowns\build\` and run the configure command again.

## Run in interactive mode

The following command opens the small GUI and starts FM TOWNS with the selected ROM directory:

```powershell
arcl_windows_fmtowns\build\bin\arcl_windows_fmtowns.exe TOWNSEMU\roms\ROM_MX
```

Example for a CD game:

```powershell
arcl_windows_fmtowns\build\bin\arcl_windows_fmtowns.exe `
  TOWNSEMU\roms\ROM_MX `
  -CD "C:\FM_TOWNS\game.cue" `
  -GAMEPORT0 KEY
```

The GUI thread performs only rendering and input processing at up to about 60 Hz. VM execution and audio generation run continuously on separate threads. Display cadence is not the audio-output cadence. `-FREQ 40` selects the VM CPU frequency; it does not set the display rate to 40 Hz.

| PC key | Action |
|---|---|
| Normal TOWNS keys | Sent directly to the guest: alphanumerics, symbols, Space, Tab, Enter, Backspace, Shift, Ctrl, Alt, cursor keys, F1–F12, and more |
| Arrow keys | Game pad 0 directions when `-GAMEPORT0 KEY` is used |
| `Z` / `X` | Game pad 0 A / B |
| `A` / `S` | Game pad 0 Run / Pause |
| `Q` | Game pad 0 Zoom |
| `Esc` | Close the ARCL window; it is not sent to the guest |

See [ARCL GUI controls](doc/reference/arcl-gui-controls.md) for display, audio, and profiling details, as well as an After Burner III example. Start with `--arcl-profile` to print per-section cumulative timings on exit.

By default, `arcl-output\\` and `arcl-state\\` are created next to the executable. Use `--arcl-output-dir DIR` and `--arcl-state-dir DIR` to change them. Explicit relative paths are resolved from the current working directory.

## Run in MCP mode

In MCP mode, the executable processes newline-delimited JSON-RPC 2.0 through standard input and output. Standard output is reserved for MCP messages, so start it as a stdio server from an MCP client rather than using it interactively in a terminal.

```powershell
arcl_windows_fmtowns\build\bin\arcl_windows_fmtowns.exe `
  --mcp --no-window --mcp-layers l0,l1 `
  --arcl-allow-root C:\FM_TOWNS `
  TOWNSEMU\roms\ROM_MX
```

Remove `--no-window` to keep the GUI visible while MCP is active. The emulator starts paused in MCP mode and advances only after the client calls `arcl_run` or `arcl_resume`. Diagnostics are written to stderr.

### MCP client configuration

[arcl_windows_fmtowns/config](arcl_windows_fmtowns/config) contains path-independent templates for Codex and Claude Code. Copy the relevant settings into local configuration, then replace the executable, permitted-media root, and ROM paths.

| Template | Purpose |
|---|---|
| [codex-config.toml.template](arcl_windows_fmtowns/config/codex-config.toml.template) | Server entry to add to Codex `config.toml` |
| [claude-code-mcp.json.template](arcl_windows_fmtowns/config/claude-code-mcp.json.template) | Server entry to add to Claude Code `.mcp.json` |

Local `.mcp.json` and `.codex/config.toml`, executable builds, ROMs, and media are intentionally ignored by Git. Do not add them to a public repository.

### Capability layers

`--mcp-layers` accepts a comma-separated subset of `control` and `l0` through `l4`. The default is `control,l0,l1`.

| Layer | Main capabilities |
|---|---|
| Control | Run, pause, reset, save, and load |
| L0 | Display capture, keyboard, mouse, joypad, and audio capture |
| L1 | Console, media control, and host directories |
| L2 | Registers, memory, breakpoints, and disassembly |
| L3 | Video, VRAM, palette, sprites, DMA, IRQ, and sound devices |
| L4 | Named snapshots, rewind, and speed measurement |

After connecting, use the JSON Schema returned by `tools/list` as the exact input/output contract for each tool. L2 and above include operations that can modify emulated state; connect only trusted MCP clients.

## Known limitations

- The target machine is FM TOWNS II MX. Also see the upstream TOWNSEMU status and limitations in [TOWNSEMU/readme.md](TOWNSEMU/readme.md).
- For media with CD audio, `.CUE` behavior depends on `PREGAP` / `POSTGAP` interpretation. Use `.MDS` / `.MDF` when reproducibility or preservation matters.
- MCP media control and shared directories are restricted to locations below `--arcl-allow-root`.
- Rewind states are not saved every frame. They are captured only when needed for explicit snapshot and rewind operations.

## Repository layout

| Path | Contents |
|---|---|
| `TOWNSEMU/` | TOWNSEMU core and necessary core changes |
| `arcl_windows_fmtowns/` | Windows frontend, MCP server, tests, and configuration templates |
| `doc/reference/` | ARCL specification, GUI controls, and validation references |
| `doc/development/` | Design, phase plans, development, and review records |
| `readme.md` | Japanese version of this README |

## License

The ARCL additions are distributed under the BSD 3-Clause License. TOWNSEMU is distributed under CaptainYS's BSD 3-Clause License; the original text is retained in [TOWNSEMU/LICENSE](TOWNSEMU/LICENSE). See [LICENSE](LICENSE) for details.

ROMs, Towns OS, game software, and disk images belong to their respective rights holders. They are not licensed by this repository and must not be bundled or redistributed.
