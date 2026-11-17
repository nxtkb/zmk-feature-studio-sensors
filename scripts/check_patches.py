#!/usr/bin/env python3
"""Verify checksums, pinned-baseline applicability, and canonical schema copies.

The supplied Git checkouts are read-only inputs. Patches are applied only inside
temporary directories, so existing local edits and build worktrees are preserved.
"""
import argparse
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile

import yaml

from export_patches import CORE_FILES, MESSAGES_BASE, ROOT, ZMK_BASE


def check(zmk: Path, messages: Path):
    projects = {
        'zmk': (zmk, ZMK_BASE, set(CORE_FILES)),
        'zmk-studio-messages': (messages, MESSAGES_BASE,
                                {'proto/zmk/keymap.proto', 'proto/zmk/sensors.proto',
                                 'proto/zmk/sensors.options'}),
    }
    entries = yaml.safe_load((ROOT / 'zephyr/patches.yml').read_text())['patches']
    if len(entries) != 2 or {entry['module'] for entry in entries} != set(projects):
        raise ValueError('Expected exactly the ZMK and messages integration patches')
    for entry in entries:
        checkout, revision, allowed = projects[entry['module']]
        actual = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != revision:
            raise ValueError(f'{checkout}: expected pinned baseline {revision}, got {actual}')
        patch = ROOT / 'zephyr/patches' / entry['path']
        content = patch.read_bytes()
        if hashlib.sha256(content).hexdigest() != entry['sha256sum']:
            raise ValueError(f'{patch}: checksum mismatch')
        paths = {line.split(' b/', 1)[1] for line in content.decode().splitlines()
                 if line.startswith('diff --git ')}
        if not paths or not paths <= allowed:
            raise ValueError(f'{patch}: unrelated or missing patch paths: {paths - allowed}')
        # Archive only existing baseline files; new schema files are created by the patch.
        tracked = subprocess.check_output(['git', '-C', str(checkout), 'ls-tree', '-r',
                                          '--name-only', 'HEAD'], text=True).splitlines()
        archive = subprocess.check_output(['git', '-C', str(checkout), 'archive', 'HEAD',
                                           '--', *sorted(paths & set(tracked))])
        with tempfile.TemporaryDirectory(prefix='studio-sensors-patch-') as temporary:
            with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
                tar.extractall(temporary, filter='data')
            for args in [('--check',), ()]:
                subprocess.run(['git', 'apply', *args, str(patch)], cwd=temporary, check=True)
            if entry['module'] == 'zmk-studio-messages':
                for name in ['sensors.proto', 'sensors.options']:
                    if (Path(temporary) / 'proto/zmk' / name).read_bytes() != (ROOT / 'protocol/local' / name).read_bytes():
                        raise ValueError(f'{name}: patch differs from the canonical local schema')
        print(f'PASS {entry["module"]}: checksum, scope and clean-baseline application')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zmk', type=Path, required=True)
    parser.add_argument('--messages', type=Path, required=True)
    args = parser.parse_args()
    check(args.zmk.resolve(), args.messages.resolve())
