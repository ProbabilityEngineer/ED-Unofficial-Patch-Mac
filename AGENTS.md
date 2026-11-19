# ED Unofficial Patch for Mac — working rules

This is the repository's agent instruction file. `CLAUDE.md` imports it; do
not duplicate instructions there. It is written for Claude Code working with
the owner, a single developer, on a Mac.

ED Unofficial Patch for Mac is a flat-only macOS/CrossOver port of
[ED Unofficial Patch](https://github.com/characterecho-sean/ed-unofficial-patch)
(ED-UP, formerly EDVR) for Elite Dangerous: Odyssey. It is one `d3d11.dll`
proxy running inside a CrossOver bottle on DXMT (Direct3D 11 over Metal). The
VR runtime, on-foot VR, installer, launcher, DLSS and XeSS were stripped
(strip stages 1-2c). Test flights cost the owner real time: distinguish local
checks from game results.

## Repositories and where things may go

| Repository | Pushes | Never |
|---|---|---|
| `ProbabilityEngineer/ED-Unofficial-Patch-Mac` (this one, formerly `ed-metalfx`; local `~/git/ed-metalfx`) | `origin`, when asked | — |
| `characterecho-sean/ed-unofficial-patch` (formerly `edvr-unofficial-patch`) | — | push, PR, issue, comment, by any route (URL, alias, API). There is no upstream remote; keep it that way. Read-only refs under `refs/up/` are local. |
| `ProbabilityEngineer/dxmt` (`~/git/forks/dxmt`) | `origin`, when asked | `3Shain/dxmt`: no push, PR, issue or comment (no-AI policy). Its `upstream` push URL is `/dev/null`. |

- **Other people's code stays local** unless the owner says otherwise, even on
  the owner's own forks. Plokhi's HDR and MetalFX work (`pr/82`) is published
  with their blessing. `private/metalfx4-friend-snapshot` (DXMT) stays local.
- **Third-party mods** (e.g. the EDUP-NHOE rumble mod) are read only: never
  pushed, patched or reported on the owner's behalf.
- **Never published:** game executables or bytecode beyond the test fixtures
  upstream already ships, Ghidra projects, decompiled material, `analysis/`
  (a separate private repository with its own `AGENTS.md`). Entire is enabled
  with uploads disabled; do not change that.

## Ask first

Each approval covers only the operation it names. Ask before:

- committing, merging, pushing, tagging for publication, releases;
- remote CI or anything metered (the owner builds locally; no CI unless asked);
- installing a DLL into a bottle or into CrossOver, or changing live settings;
- destructive operations, including deleting branches, killing processes and
  overwriting files you have not looked at;
- removing or renaming a config key or value: quote its name and current
  behaviour; once approved, list it in the commit message.

Scope: relevant inspection, edits, focused tests, local builds and self-tests
are included once work is approved. Report adjacent problems instead of
silently fixing them; no unrelated refactoring.

## The owner's priorities

- **No graphical risk for small performance gains.** A fix that might show an
  artefact needs a clear reason.
- **Colour managed as correctly as possible.** The current star light colour
  (game-based tint) is the chosen one; a physical tint was tried and rejected.
- Plain, direct explanations. Say what was verified, what is a hypothesis, and
  what a flight would show.

## Machine, bottles and logs

- **Arm bottle:** `Elite-Arm` in CrossOver Preview (`/Applications/CrossOver Preview.app`),
  the main test rig. **Intel bottle:** `Elite`. Bottles live in
  `~/Library/Application Support/CrossOver/Bottles/`.
- **Game folder:** `drive_c/Program Files (x86)/Frontier/EDLaunch/Products/elite-dangerous-odyssey-64`.
  Logs: `edvr_logs/edvr_gfx_*.log` there. The first line names the build
  (`git describe`) and link time: establish which build produced a log before
  attributing behaviour to it. `tools/edvr_log.py` reads them.
- **DXMT in use:** CrossOver Preview `Contents/SharedSupport/CrossOver/lib/dxmt`.
  Stock is kept as `lib/dxmt-orig`; other builds sit beside it. The log says
  `discard workaround off` when the DXMT reports the write-mask fix.
- **Is Elite running?** `pgrep -f EliteDangerous >/dev/null; echo $?`. Never
  print `pgrep -fl` output or the game's command line: it carries auth tokens.
  Installs need only Elite closed; the launcher may stay open.
- Live `edvr-flat.ini` (CRLF) is the owner's state: read it freely, change it
  only when asked, key by key, and show the diff.

## Building

- Local graphics DLL: `python3 tools/linux_toolchain.py build-graphics` (MSVC
  under CrossOver; cold about 2 min, warm about 30 s). Builds are kept in
  `build/graphics-builds/<time>-<branch>-<describe>/`. Redirect the output to
  a file in the scratchpad; never pipe a Wine build through `tail`.
- `build.bat` (full build, tests, receipts) needs Windows and is not part of
  routine work; say when a C++ test could not be run here. Never bypass or
  forge build gates or receipts.
- DXMT: build in `~/git/forks/dxmt` (`build/<name>-x64` with `toolchains/wine`,
  `build/<name>-arm64ec` with the wine prefix in `~/git/wine-prefix`).
- Run a tool's own `--self-test` when changing it. `edvr_log.py --self-test`
  skips, by name, pins on sources the flat-only port removed.
- Line endings: `build.bat` is CRLF (edit byte-safely), the repo `edvr.ini` is
  LF.

## Git

- Branch per piece of work; merge to `main` with `--no-ff` when approved.
- Never a bare `git stash`/`pop` (the stack is shared across worktrees).
- Commits end with `Co-Authored-By: Claude <model> <noreply@anthropic.com>`
  as the session's attribution says.
- **Versions:** builds are named by `git describe --exclude 'feature/*'`. The
  base tag is `v0.22.3-mfx` (upstream 0.22.0 plus its 0.22.1-0.22.3 fixes);
  releases are `macos-vX.Y.Z`, starting at `macos-v0.23.0`.
- **Tags:** `feature/<name>` marks the commit where a feature became complete
  (add one when a feature lands); `archive/<name>` keeps an unmerged branch
  after the branch is deleted.
- **The to-do list** is `docs/todo.md` on the local branch `todo`, checked out
  at `~/git/ed-metalfx-todo`. Keep it current when the backlog changes and
  commit it there. Never merge it into `main` and never push it: it is private
  working notes.

## Releases

A release is two zips on the GitHub release `macos-vX.Y.Z`:

1. `d3d11.dll`, `release/mac/edvr-flat.ini` (the defaults), the licence
   notices (`LICENSE`, `FIDELITYFX-SDK-DX11-LICENSE.txt`,
   `DXBC-HASH-LICENSE.txt`, `MINIZ-LICENSE.txt`);
2. `dxmt-<version>.zip`: the DXMT build's `aarch64-*` and `x86_64-*` folders,
   with DXMT's licence and the source commit.

Build both from clean commits (no `-dirty`). Installation is manual (README)
until an installer exists. The repository is private until the owner decides
to make it public.

## Runtime diagnosis

- Distinguish "never ran", "refused", "accepted" and "correct output".
- Prefer root causes over cosmetic compensation; record important ruled-out
  hypotheses briefly in the relevant doc.
- Do not ask for a flight when an existing log answers the question. When a
  flight is needed, say exactly what to do and what the log or the screen
  will show either way.
- Preserve hook displacement checks, protection transitions and recovery
  paths. Under Wine and FEX, address space near the game image is scarce:
  `src/common/code_hook.cpp` reserves near regions at DLL load.

## Layout

- `src/d3d11/`: graphics hooks and fixes; `src/common/`: shared code.
- `tools/`: build, log and test tooling; `tools/macos_wine/`: CrossOver shims.
- `docs/`: human-facing docs (many are upstream's VR-era design notes);
  `docs/building.md` has the macOS build. `.agents/`: agent-only handoffs
  (export-ignored).
- `edvr.ini`: every setting, documented; `release/mac/`: release files.
- `CODING_STANDARDS.md`: code-review and correctness rules.
