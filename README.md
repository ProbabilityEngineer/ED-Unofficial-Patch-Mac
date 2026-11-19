# ED Unofficial Patch for Mac

**ED Unofficial Patch for Mac** is an independent macOS/CrossOver port of
[ED Unofficial Patch](https://github.com/characterecho-sean/ed-unofficial-patch)
(ED-UP, formerly EDVR) by characterecho-sean, for Elite Dangerous: Odyssey.
It is not an official upstream release, and upstream does not support it:
please report problems here, not to the upstream project.

It is for **Mac users running the Windows game in CrossOver** with the DXMT
graphics backend (Direct3D 11 translated to Metal).

## Release parity and provenance

This port is based on upstream **0.22.3**: upstream 0.22.0, plus its 0.22.1 to
0.22.3 fixes picked in selectively (git tag `v0.22.3-mfx`). Mac releases use
their own version line, tagged `macos-vX.Y.Z`, starting at `macos-v0.23.0`.
Upstream credit remains with characterecho-sean and the ED-UP contributors.

## What is different from upstream

- **Flat screen only.** The VR runtime (OpenXR, OpenVR), native on-foot VR,
  the installer and launcher, DLSS and XeSS are removed: none of them run on a
  Mac.
- **MetalFX anti-aliasing and upscaling.** Apple's MetalFX temporal scaler,
  reached through DXMT, next to FSR 3.1 (F8 menu, Anti-aliasing).
- **HDR output** on EDR displays. Scene white follows macOS's own SDR white
  live by default (`auto`), so HDR brightness tracks the display brightness.
- **Star-coloured lighting.** Light in a system takes the colour of its star,
  through light buffers, atmospheres and ring dust.
- **Workarounds for DXMT**, such as station window decorations and logos kept
  visible on stock DXMT.

The settings and fixes otherwise follow upstream:
[docs/fixes.md](docs/fixes.md) describes them, though parts of it are about VR
and do not apply here.

## Requirements

- CrossOver, with the game's bottle set to the **DXMT** graphics backend.
- Elite Dangerous: Odyssey (live build; checked against game build 334925).

### Modified DXMT (recommended)

Stock DXMT works, with two known problems that a modified DXMT
([ProbabilityEngineer/dxmt](https://github.com/ProbabilityEngineer/dxmt),
branch `private/writemask-fix-feature`) fixes:

- **Memory grows when you change MetalFX settings.** Stock DXMT keeps every
  MetalFX scaler it has made and never frees one. Each time you switch MetalFX
  off and on, or change Supersampling, it makes another, and memory use can
  climb by gigabytes. On stock DXMT, restart Elite after experimenting.
- **Some decals with a partial write mask.** ED-UP for Mac works around this
  on stock DXMT by keeping the game's own shader for those draws, which costs
  only per-object motion for them.

## Install

1. Copy `d3d11.dll` into the game folder, next to `EliteDangerous64.exe`
   (`Products/elite-dangerous-odyssey-64` inside the bottle).
2. Optionally copy `edvr-flat.ini` there too; a missing file means the
   defaults.
3. Start the game. Press **F8** for the settings menu.

Frontier's launcher may remove `d3d11.dll` when it verifies the install; copy
it back if it does.

### Checking it worked

The game folder gets an `edvr_logs` folder, and each session writes an
`edvr_gfx_*.log` there. Its first line names the version.

## Settings

Settings are in `edvr-flat.ini` next to the game; F8 changes them in the game.
Each setting is documented in the shipped `edvr.ini`.

## Running alongside other mods

EDHM also installs itself as `d3d11.dll`. Rename EDHM's file and name it in
`edvr-flat.ini` as `real_dll`; ED-UP for Mac then passes everything through
EDHM. [docs/manual-install.md](docs/manual-install.md#running-alongside-edhm-by-hand)
has the procedure.

## Build

The graphics DLL builds on macOS under CrossOver;
[docs/building.md](docs/building.md#macos-graphics-only-builds-under-crossover)
has the steps.

## Credits

- [characterecho-sean](https://github.com/characterecho-sean) and the ED
  Unofficial Patch contributors: the patch this port is built on.
- [Plokhi](https://github.com/plokhi): foundational work on HDR output and on
  MetalFX anti-aliasing and supersampling.
- [arturbac](https://github.com/arturbac): the constant-buffer readback work
  upstream (issue #65, PR #66) that this port inherits.
- [DXMT](https://github.com/3Shain/dxmt) by Feifan He (3Shain) for
  CodeWeavers: the Direct3D 11 to Metal layer this port runs on. DXMT's
  authors do not support this port; please do not report its problems to them.

## Licence and standing

ED Unofficial Patch is MIT licensed; see [LICENSE](LICENSE), which keeps
upstream's notice. The `fsr` engine is AMD's FidelityFX Super Resolution 3.1
upscaler through its community Direct3D 11 port (the optiscaler project's
FidelityFX-SDK-DX11, MIT), compiled into `d3d11.dll`; its notice ships as
`FIDELITYFX-SDK-DX11-LICENSE.txt`. The DXBC hash and miniz notices ship as
`DXBC-HASH-LICENSE.txt` and `MINIZ-LICENSE.txt`. DXMT is LGPL; the modified DXMT's source is
in the fork named above.

Not affiliated with, endorsed by, or supported by Frontier Developments plc,
Apple Inc., CodeWeavers or the upstream ED Unofficial Patch project. Elite
Dangerous is a trademark of Frontier Developments plc.
