#!/usr/bin/env python3
"""Run a UI application with its own macOS bundle identity during development."""
import argparse
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil

from otium_tools import BUILD

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app-name', default='Otium UI')
    parser.add_argument('--app-id', default='org.otium.ui-demo')
    parser.add_argument('--app-version', default='0.1')
    parser.add_argument('--icon', default='native/ui/assets/otium.png')
    parser.add_argument('--application', default='examples/ui.ot')
    parser.add_argument('arguments', nargs=argparse.REMAINDER,
                        help='Application arguments after --, e.g. -- --headless --frames 3')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('This development bundle launcher is macOS-only; use ./otium elsewhere')
    if not args.app_name.strip() or any(c in args.app_name for c in '/:\0'):
        parser.error('Application name must be a nonempty filename without / or :')
    if not re.fullmatch(r'[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+', args.app_id):
        parser.error('Application ID must be a reverse-domain identifier')
    runtime = (BUILD/'otium').resolve()
    if not runtime.is_file():
        parser.error('Runtime is missing; run just build first')
    application = (ROOT/args.application).resolve()
    if not application.is_file():
        parser.error('Application source does not exist')
    icon = str((ROOT/args.icon).resolve()) if args.icon else ''
    if icon and not Path(icon).is_file():
        parser.error('Icon does not exist')
    bundle = ROOT/'build/apps'/args.app_id/(args.app_name+'.app')
    contents = bundle/'Contents'
    (contents/'MacOS').mkdir(parents=True, exist_ok=True)
    host = contents/'MacOS'/'otium'
    # Use a fresh inode: another instance may still be executing the old host.
    temporary = host.with_suffix('.new')
    shutil.copy2(runtime, temporary)
    temporary.replace(host)
    info = {'CFBundleExecutable': 'otium', 'CFBundleName': args.app_name,
            'CFBundleDisplayName': args.app_name, 'CFBundleIdentifier': args.app_id,
            'CFBundlePackageType': 'APPL', 'CFBundleVersion': args.app_version,
            'CFBundleShortVersionString': args.app_version, 'NSHighResolutionCapable': True}
    (contents/'Info.plist').write_bytes(plistlib.dumps(info))
    options = args.arguments[1:] if args.arguments[:1] == ['--'] else args.arguments
    command = [str(host), '--no-project', '-L', str(ROOT/'lib'),
               '-L', str(ROOT/'examples'), '-L', str(ROOT/'tests'), str(application), '--',
               '--app-name', args.app_name, '--app-id', args.app_id,
               '--app-version', args.app_version, '--icon', icon, *options]
    os.environ.setdefault('OTIUM_FFI_LIBRARY', str(BUILD/'libotium-ffi.dylib'))
    os.chdir(ROOT)
    os.execv(str(host), command)


if __name__ == '__main__':
    main()
