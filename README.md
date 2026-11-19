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
  visible on stock DXMT, plus [DXMT fixes](#dxmt-fixes-separate-download-recommended)
  as a separate download.

The settings and fixes otherwise follow upstream:
[docs/fixes.md](docs/fixes.md) describes them, though parts of it are about VR
and do not apply here.

## Star-coloured lighting

Elite lights every system with near-white light, whatever the colour of its
star. With star-coloured lighting, ships, stations, planets, atmospheres and
ring dust are lit in the colour of the system's star: warm under an orange K
star, cool under a blue-white B star. The tint comes from the colour the game
itself gives each star, so it matches the star you see. Setting: F8, Visual,
**Star light colour** (0 to 100%, 0 being the game's own white; default 100).

Two other ways of colouring the light were built and tried, and can come back
as options if people want them:

- **Hue-based**, with saturation and luminosity relative to the star's.
- **Physics-based** star colours, from each star's temperature.

## Requirements

- CrossOver, with the game's bottle set to the **DXMT** graphics backend
  (bottle settings, Advanced, Graphics).
- Elite Dangerous: Odyssey (live build; checked against game build 334925).

## DXMT fixes (separate download, recommended)

Each release has a second download: DXMT, the Direct3D 11 to Metal layer
CrossOver uses, with fixes for Elite, from the `elite-mac` branch of
[ProbabilityEngineer/dxmt](https://github.com/ProbabilityEngineer/dxmt/tree/elite-mac).
Its releases are tagged `elite-mac-vX.Y.Z`, matching this project's
`macos-vX.Y.Z` (for `macos-v0.23.0`, use
[`elite-mac-v0.23.0`](https://github.com/ProbabilityEngineer/dxmt/releases/tag/elite-mac-v0.23.0)). ED Unofficial Patch
for Mac works without it, with these differences:

- **Memory.** On stock DXMT, changing the game's screen resolution several
  times with MetalFX on adds memory each time (stock DXMT never frees a
  MetalFX upscaler); restart Elite after trying a few. Supersampling changes
  do not do this: ED Unofficial Patch for Mac keeps MetalFX at one size per
  screen resolution.
- **Decals.** Some decals, such as station window logos, need a DXMT fix
  (partial write masks). Without it, ED Unofficial Patch for Mac keeps the
  game's own shader for those draws, so they still show, but they lose their
  own motion for the anti-aliasing.

## Install

Installation is manual for now. Quit Elite first.

### 1. ED Unofficial Patch for Mac (the game folder)

1. In CrossOver, select the Elite bottle and choose **Open C: Drive**.
2. Go to the game folder: `Program Files (x86)/Frontier/EDLaunch/Products/elite-dangerous-odyssey-64`
   for the Frontier launcher, or the game's folder under
   `Program Files (x86)/Steam/steamapps/common/Elite Dangerous/Products/`
   for Steam. It is the folder with `EliteDangerous64.exe` in it.
3. Copy `d3d11.dll` and `edvr-flat.ini` from the release zip into it. Keep
   your own `edvr-flat.ini` if you already have one.
4. Start the game and press **F8** for the settings menu.

Frontier's launcher may remove `d3d11.dll` when it verifies the game files;
copy it back if it does.

### 2. DXMT fixes (CrossOver's DXMT folder, optional)

This changes CrossOver itself, so it applies to every bottle that uses DXMT,
and a CrossOver update puts the stock DXMT back (install it again after
updating).

1. In Finder, go to Applications, right-click **CrossOver** and choose
   **Show Package Contents**, then open `Contents/SharedSupport/CrossOver/lib/dxmt`.
2. **Back up first:** copy the whole `dxmt` folder somewhere safe (or
   duplicate it next to itself as `dxmt-orig`).
3. Copy the folders from the DXMT zip's `dxmt-elite-mac-vX.Y.Z` folder (`aarch64-unix`, `aarch64-windows`,
   `x86_64-unix`, `x86_64-windows`) over the ones in `dxmt`, replacing the
   files. Copy over the existing files; do not replace the folder, because
   CrossOver's folder also holds 32-bit files the zip does not include.
4. Start the game. The log (below) says `discard workaround off` when the
   fixed DXMT is in use.

To undo it, put the backup back.

### Checking it worked

The game folder gets an `edvr_logs` folder, and each session writes an
`edvr_gfx_*.log` there. Its first line names the version.

## Settings

Settings are in `edvr-flat.ini` next to the game; F8 changes them in the game.
[edvr.ini](edvr.ini) in this repository documents every setting. The settings
files keep their ED-UP (formerly EDVR) names, so existing ones keep working.

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
`DXBC-HASH-LICENSE.txt` and `MINIZ-LICENSE.txt`. DXMT is LGPL; the DXMT fixes' source is in
[ProbabilityEngineer/dxmt](https://github.com/ProbabilityEngineer/dxmt).

Not affiliated with, endorsed by, or supported by Frontier Developments plc,
Apple Inc., CodeWeavers or the upstream ED Unofficial Patch project. Elite
Dangerous is a trademark of Frontier Developments plc.
