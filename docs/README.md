# Documentation

These are the documents that apply to ED Unofficial Patch for Mac: the flat
game on macOS under CrossOver and DXMT.

ED Unofficial Patch's other documents (VR and OpenXR, native on-foot VR, the
installer and launcher, DLSS and XeSS, Linux and Proton, Windows release CI,
and older investigations) were removed from this port. They are upstream, at
the version this port is based on:
[characterecho-sean/ed-unofficial-patch, v0.22.3, docs/](https://github.com/characterecho-sean/ed-unofficial-patch/tree/v0.22.3/docs).
A link in a document here that leads nowhere points to one of those.

## Start here

- [fixes.md](fixes.md): every fix and feature, with its setting (its VR parts
  do not apply here).
- [settings-menu.md](settings-menu.md): the F8 menu.
- [troubleshooting.md](troubleshooting.md).
- [manual-install.md](manual-install.md#running-alongside-edhm-by-hand):
  running alongside EDHM.
- [building.md](building.md#macos-graphics-only-builds-under-crossover):
  building on a Mac.

## Mac-specific

- [macos-dxmt-2026-09-30.md](macos-dxmt-2026-09-30.md): Elite on DXMT.
- [edvr-unified-hdr-2026-11-01.md](edvr-unified-hdr-2026-11-01.md): HDR output.
- [memory-growth-2026-10-22.md](memory-growth-2026-10-22.md).

## Design notes and investigations

The other documents here are design notes and investigations behind code this
port still runs: anti-aliasing and motion, the flat camera, stations and
culling, the interface layer, loading screens, star glow, billboards and the
game build. Source comments name the one they belong to.
