#!/usr/bin/env python3
"""Generate the graphics DLL's version header and version resource.

--version-header writes edvr_version.h (EDVR_VERSION_STRING, the `git describe`
string the log's first lines print); --version-rc writes a standalone .rc
holding only a VERSIONINFO. build.bat runs both. flat_ini_bytes() is the
default edvr-flat.ini that tools/install_edvr.py writes.

The name is historical: until 2026-11-21 this also generated the Windows
installer's and launcher's resource scripts (payload, manifest, icon). Those
went with the installer; the Mac port installs by hand (README) until a Mac
installer exists.

Usage:
  python tools/gen_installer_rc.py --version-header --version <string> --out <gen dir>
  python tools/gen_installer_rc.py --version-rc graphics --version <string> --out <gen dir>
"""

import argparse
import os
import re
import subprocess
import sys


def flat_ini_bytes():
    """The shipped edvr-flat.ini: the flat profile's own default settings."""
    return (b'# Experimental flat temporal profile: visual qualification in progress.\r\n'
            b'# F8 opens the AA menu: Off / TAA / DLSS / FSR3 / XeSS-SR and DLSS model presets.\r\n'
            b'# temporal_aa: off, on (TAA), dlaa (native SS), dlss, fsr, xess. Game SS controls render scale.\r\n'
            b'# XeSS-SR needs libxess.dll beside the game; the flat installer includes it when built with the SDK.\r\n'
            b'# Press NumLock while the issue is visible to collect a diagnostic capture.\r\n'
            b'[fix]\r\ntemporal_aa = off\r\ntemporal_aa_model = k\r\nui_quality = 100\r\n\r\n'
            b'[visual]\r\n'
            b'# intro_video: screen plays the launch movie as the game makes it; skip refuses it at launch.\r\n'
            b'# moved-from: fix.intro_video (default screen)\r\n'
            b'intro_video = screen\r\n\r\n'
            b'# compare_aa: F7 turns AA off to compare and back; runtime only, nothing is written.\r\n'
            b'[hotkey]\r\nmenu = F8\r\ncompare_aa = F7\r\ndump_draws = NUMLOCK\r\n\r\n'
            b'[diag]\r\n'
            b'# Temporary F1 origin/source/optical/world capture. Restart the game after changing this value.\r\n'
            b'# Does not change the production origin-ambiguity reset. Flat startup only.\r\n'
            b'flat_origin_capture = off\r\n\r\n'
            b'[log]\r\nenabled = 1\r\n\r\n'
            b'[advanced]\r\n'
            b'# The mod EDVR chains through (EDHM, ReShade) is set once, in edvr.ini, and applies to the flat profile too.\r\n'
            b'# Leave this empty to use that. A file name here overrides it for flat; none runs flat without the chain.\r\n'
            b'real_dll =\r\n')


def parse_version_tuple(describe):
    """Parse a `git describe` string into a numeric (major, minor, patch,
    commits) tuple for FILEVERSION/PRODUCTVERSION.

    'v0.17.0-rc.2-16-g2c6e820' -> (0, 17, 0, 16): the trailing '-<n>-g<hash>'
    `git describe` appends past the tag is the commit count since it; a
    '-rc.N' pre-release suffix and a trailing '-dirty' are not fields
    VERSIONINFO has room for and are dropped either way. A bare tag
    ('v0.16.2', 'v0.17.0-rc.3') has no '-<n>-g<hash>' suffix and reads as
    zero commits since it. Anything that does not start with a dotted
    major.minor.patch -- 'unknown' (no repo) included -- is unparsable and
    becomes the all-zero version Windows shows for "no version". Every field
    is clamped to 0..65535, VERSIONINFO's numeric field width.
    """
    m = re.match(r'^v?(\d+)\.(\d+)\.(\d+)(?:-.*)?$', describe or '')
    if not m:
        return (0, 0, 0, 0)
    major, minor, patch = (int(m.group(i)) for i in (1, 2, 3))
    tail = re.search(r'-(\d+)-g[0-9a-f]+(?:-dirty)?$', describe)
    commits = int(tail.group(1)) if tail else 0

    def clamp(n):
        return max(0, min(65535, n))
    return tuple(clamp(n) for n in (major, minor, patch, commits))


def origin_url_https():
    """The origin remote as an https://github.com/... link with no trailing
    .git, for a Comments string that gives someone skeptical of an unsigned
    binary somewhere to check it. `git remote get-url origin` rather than a
    hand-typed constant for the same reason EDVR_VER is `git describe`: a
    repo that moves keeps a hand-typed URL that is quietly wrong. No git, no
    repo, or no `origin` remote falls back to the project's known home rather
    than failing a build over a Comments string.
    """
    fallback = 'https://github.com/characterecho-sean/ed-unofficial-patch'
    try:
        url = subprocess.check_output(
            ['git', 'remote', 'get-url', 'origin'],
            stderr=subprocess.DEVNULL, timeout=10,
        ).decode('utf-8', 'replace').strip()
    except Exception:
        return fallback
    if url.endswith('.git'):
        url = url[:-len('.git')]
    m = re.match(r'^git@([^:]+):(.+)$', url)
    if m:
        return 'https://%s/%s' % (m.group(1), m.group(2))
    return url if url.startswith('http://') or url.startswith('https://') else fallback


def versioninfo_lines(version, file_description, internal_name, original_filename,
                      file_type, comments):
    """The VERSIONINFO block text, the single writer for it: every .rc file
    this module produces calls this rather than keeping its own copy of the
    eleven lines of StringFileInfo boilerplate.
    """
    major, minor, patch, commits = parse_version_tuple(version)
    return [
        '1 VERSIONINFO',
        'FILEVERSION %d,%d,%d,%d' % (major, minor, patch, commits),
        'PRODUCTVERSION %d,%d,%d,%d' % (major, minor, patch, commits),
        'FILEFLAGSMASK 0x3fL',
        'FILEFLAGS 0x0L',
        'FILEOS 0x40004L',
        'FILETYPE %s' % file_type,
        'FILESUBTYPE 0x0L',
        'BEGIN',
        '    BLOCK "StringFileInfo"',
        '    BEGIN',
        '        BLOCK "040904b0"',
        '        BEGIN',
        '            VALUE "CompanyName", "ED-UP (unofficial)"',
        '            VALUE "FileDescription", "%s"' % file_description,
        '            VALUE "FileVersion", "%s"' % version,
        '            VALUE "InternalName", "%s"' % internal_name,
        '            VALUE "OriginalFilename", "%s"' % original_filename,
        '            VALUE "ProductName", "ED Unofficial Patch"',
        '            VALUE "ProductVersion", "%s"' % version,
        '            VALUE "LegalCopyright", "Copyright (c) 2026 characterecho-sean. MIT License."',
        '            VALUE "Comments", "%s"' % comments,
        '        END',
        '    END',
        '    BLOCK "VarFileInfo"',
        '    BEGIN',
        '        VALUE "Translation", 0x409, 1200',
        '    END',
        'END',
    ]


def write_version_rc(which, version, out, dry_run):
    """--version-rc graphics|runtime: a standalone .rc holding only a
    VERSIONINFO, for the two DLLs that otherwise ship with no resource naming
    them at all: nothing but version strings.
    """
    comments = 'Source and releases: %s' % origin_url_https()
    if which == 'graphics':
        lines = versioninfo_lines(
            version,
            file_description=('ED Unofficial Patch for Elite Dangerous: Direct3D 11 '
                              'proxy (forwards to the system d3d11.dll)'),
            internal_name='edvr_openxr_graphics',
            original_filename='d3d11.dll',
            file_type='0x2L',
            comments=comments)
        filename = 'version_graphics.rc'
    else:
        lines = versioninfo_lines(
            version,
            file_description=('ED Unofficial Patch for Elite Dangerous: OpenXR runtime '
                              'speaking the OpenVR ABI'),
            internal_name='edvr_openxr_runtime',
            original_filename='openvr_api.dll',
            file_type='0x2L',
            comments=comments)
        filename = 'version_runtime.rc'

    if dry_run:
        print('gen_installer_rc: dry run (%s); wrote nothing' % which)
        return 0
    os.makedirs(out, exist_ok=True)
    out_path = os.path.join(out, filename)
    header = ['// Generated by tools/gen_installer_rc.py. Do not edit; do not commit.', '']
    with open(out_path, 'w', encoding='utf-8', newline='\r\n') as f:
        f.write('\n'.join(header + lines + ['']))
    print('gen_installer_rc: wrote %s' % out_path)
    return 0


def c_escape(text):
    """A string as the body of a C string literal: backslash and double quote."""
    return text.replace('\\', '\\\\').replace('"', '\\"')


def version_header_text(version):
    """The text of edvr_version.h for this version string."""
    return ('// Generated by tools/gen_installer_rc.py. Do not edit; do not commit.\n'
            '#pragma once\n'
            '#define EDVR_VERSION_STRING "%s"\n' % c_escape(version))


def write_version_header(version, out, dry_run):
    """--version-header: edvr_version.h, so the version is an include rather
    than a /D flag on every translation unit. A flag changes with every commit
    and recompiles everything; the header is rewritten only when its content
    differs, so an unchanged version leaves the file (and its mtime) alone.
    """
    out_path = os.path.join(out, 'edvr_version.h')
    text = version_header_text(version)
    data = text.encode('utf-8')
    try:
        with open(out_path, 'rb') as f:
            same = f.read() == data
    except OSError:
        same = False
    if same:
        print('gen_installer_rc: unchanged %s' % out_path)
        return 0
    if dry_run:
        print('gen_installer_rc: dry run (version header); wrote nothing')
        return 0
    os.makedirs(out, exist_ok=True)
    with open(out_path, 'wb') as f:
        f.write(data)
    print('gen_installer_rc: wrote %s' % out_path)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--out')
    ap.add_argument('--version', default='unknown')
    ap.add_argument('--version-rc', choices=('graphics', 'runtime'),
                    help='write a standalone VERSIONINFO .rc for this DLL into --out')
    ap.add_argument('--version-header', action='store_true',
                    help='write edvr_version.h (#define EDVR_VERSION_STRING) into --out, '
                         'only when its content changes')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.version_header:
        if not args.out:
            ap.error('--out is required with --version-header')
        return write_version_header(args.version, args.out, args.dry_run)
    if args.version_rc:
        if not args.out:
            ap.error('--out is required with --version-rc')
        return write_version_rc(args.version_rc, args.version, args.out, args.dry_run)
    ap.error('choose --version-header or --version-rc (the installer payload went with the installer)')


def self_test():
    import tempfile

    # The describe-string -> numeric-tuple parser, used for FILEVERSION and
    # PRODUCTVERSION in every VERSIONINFO block this module writes.
    assert parse_version_tuple('v0.17.0-rc.2-16-g2c6e820') == (0, 17, 0, 16)
    assert parse_version_tuple('v0.16.2') == (0, 16, 2, 0)
    assert parse_version_tuple('v0.17.0-rc.3') == (0, 17, 0, 0)
    assert parse_version_tuple('unknown') == (0, 0, 0, 0)
    assert parse_version_tuple('garbage') == (0, 0, 0, 0)
    assert parse_version_tuple('v1.2.3-70000-gabcdef1') == (1, 2, 3, 65535)  # clamped
    assert parse_version_tuple('0.1.0') == (0, 1, 0, 0)  # no "v" and no suffix

    # The default edvr-flat.ini install_edvr.py writes (and aa_compare_test pins the F7 line of).
    flat_ini = flat_ini_bytes()
    assert b'temporal_aa = off' in flat_ini and b'menu = F8' in flat_ini
    assert b'[diag]\r\n' in flat_ini and b'flat_origin_capture = off\r\n' in flat_ini
    assert b'dump_draws = NUMLOCK' in flat_ini and b'dump_draws = F10' not in flat_ini
    assert b'compare_aa = F7' in flat_ini and b'nothing is written' in flat_ini
    # The key lives in [visual] (2026-10-22), and the annotation sits directly above it so a merge carries a flat user's old
    # [fix] intro_video to the new name (iniedit.cpp movedKeys: a section line or a blank clears a pending annotation).
    assert (b'[visual]\r\n# intro_video: screen plays the launch movie as the game makes it; skip refuses it at launch.\r\n'
            b'# moved-from: fix.intro_video (default screen)\r\nintro_video = screen\r\n') in flat_ini
    assert b'\n' not in flat_ini.replace(b'\r\n', b'')

    with tempfile.TemporaryDirectory(prefix='edvr-rc-test-') as root:
        # --version-rc: a standalone VERSIONINFO .rc.
        rc_out = os.path.join(root, 'rc_out')
        assert main(['--version-rc', 'graphics', '--version', 'v0.17.0-rc.2-16-g2c6e820',
                     '--out', rc_out, '--dry-run']) == 0 and not os.path.exists(rc_out)
        assert main(['--version-rc', 'graphics', '--version', 'v0.17.0-rc.2-16-g2c6e820',
                     '--out', rc_out]) == 0
        with open(os.path.join(rc_out, 'version_graphics.rc'), 'r', encoding='utf-8') as stream:
            graphics_text = stream.read()
        assert 'OriginalFilename", "d3d11.dll"' in graphics_text
        assert 'FILEVERSION 0,17,0,16' in graphics_text
        assert 'FileVersion", "v0.17.0-rc.2-16-g2c6e820"' in graphics_text

        assert main(['--version-rc', 'runtime', '--version', 'v0.16.2', '--out', rc_out]) == 0
        with open(os.path.join(rc_out, 'version_runtime.rc'), 'r', encoding='utf-8') as stream:
            runtime_text = stream.read()
        assert 'OriginalFilename", "openvr_api.dll"' in runtime_text
        assert 'FILEVERSION 0,16,2,0' in runtime_text

        # Neither option: refused, since the installer payload this used to write is gone.
        import contextlib, io
        refusal = io.StringIO()
        try:
            with contextlib.redirect_stderr(refusal):
                main(['--out', rc_out])
        except SystemExit as exc:
            assert exc.code == 2 and 'choose --version-header or --version-rc' in refusal.getvalue()
            print('expected-error: no option chosen (exit 2)')
        else:
            raise AssertionError('a run with neither option was accepted')

        # --version-header: escaped define, rewritten only when the version changes.
        vh_out = os.path.join(root, 'vh_out')
        vh_path = os.path.join(vh_out, 'edvr_version.h')
        assert main(['--version-header', '--version', 'v1', '--out', vh_out,
                     '--dry-run']) == 0 and not os.path.exists(vh_out)
        assert main(['--version-header', '--version', 'v1.0-3-gabc-dirty',
                     '--out', vh_out]) == 0
        with open(vh_path, 'rb') as stream:
            vh_bytes = stream.read()
        assert b'#pragma once' in vh_bytes
        assert b'#define EDVR_VERSION_STRING "v1.0-3-gabc-dirty"\n' in vh_bytes
        os.utime(vh_path, (1000000000, 1000000000))
        assert main(['--version-header', '--version', 'v1.0-3-gabc-dirty',
                     '--out', vh_out]) == 0
        assert os.stat(vh_path).st_mtime == 1000000000   # unchanged: not rewritten
        assert main(['--version-header', '--version', 'v1.0-4-gdef',
                     '--out', vh_out]) == 0
        assert os.stat(vh_path).st_mtime != 1000000000
        with open(vh_path, 'rb') as stream:
            assert b'"v1.0-4-gdef"' in stream.read()
        assert main(['--version-header', '--version', 'a"b\\c', '--out', vh_out]) == 0
        with open(vh_path, 'rb') as stream:
            assert b'#define EDVR_VERSION_STRING "a\\"b\\\\c"\n' in stream.read()
    print('gen_installer_rc: self-test passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
