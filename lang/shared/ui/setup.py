#!/usr/bin/env python3
"""Install the pinned Yoga source archive into the local build directory."""
import argparse
import hashlib
from pathlib import Path
import shutil
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
VERSION = '3.2.1'
SHA256 = '86b399ac31fd820d8ffa823c3fae31bb690b6fc45301b2a8a966c09b5a088b55'
URL = f'https://github.com/facebook/yoga/archive/refs/tags/v{VERSION}.tar.gz'
DEST = ROOT / 'shared/build/deps' / f'yoga-{VERSION}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--download', action='store_true')
    args = parser.parse_args()
    if (DEST / '.otium-sha256').is_file() and (DEST / '.otium-sha256').read_text() == SHA256:
        return
    if not args.archive and not args.download:
        parser.error('Yoga is missing; run make ui-setup or supply --archive FILE')
    DEST.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=DEST.parent) as temp:
        temp = Path(temp)
        archive = args.archive or temp / 'yoga.tar.gz'
        if args.download and not args.archive:
            urllib.request.urlretrieve(URL, archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
            raise SystemExit('Yoga archive checksum mismatch')
        with tarfile.open(archive) as source:
            source.extractall(temp, filter='data')
        extracted = temp / f'yoga-{VERSION}'
        # Only the native library and its license are needed.
        stage = temp / 'install'
        stage.mkdir()
        shutil.copytree(extracted / 'yoga', stage / 'yoga')
        shutil.copy2(extracted / 'LICENSE', stage / 'LICENSE')
        (stage / '.otium-sha256').write_text(SHA256)
        if DEST.exists():
            shutil.rmtree(DEST)
        stage.rename(DEST)
    print(f'Yoga {VERSION}: {DEST}')


if __name__ == '__main__':
    main()
