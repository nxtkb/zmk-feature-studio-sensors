#!/usr/bin/env python3
"""Build the exact production C engine/RPC with a test-only host port."""
from pathlib import Path
import argparse
import shutil
import subprocess
import sys

def build(nanopb: Path, messages: Path, output: Path):
    root = Path(__file__).resolve().parents[1]
    output.mkdir(parents=True, exist_ok=True)
    generated = output / 'proto/zmk'; generated.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(nanopb / 'generator/nanopb_generator.py'),
        '-I', str(root / 'protocol/local'), '-D', str(generated),
        str(root / 'protocol/local/sensors.proto')], check=True)
    library = output / ('libstudio_sensors.dylib' if sys.platform == 'darwin' else 'libstudio_sensors.so')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
        '-shared', '-fPIC', '-pthread', '-g', '-I' + str(root / 'include'), '-I' + str(nanopb), '-I' + str(output),
        str(root / 'src/engine.c'), str(root / 'src/rpc.c'), str(root / 'tests/host_port.c'),
        str(generated / 'sensors.pb.c'), str(nanopb / 'pb_encode.c'), str(nanopb / 'pb_decode.c'),
        str(nanopb / 'pb_common.c'), '-o', str(library)], check=True)
    python = output / 'python'; python.mkdir(exist_ok=True)
    subprocess.run([shutil.which('protoc'), '-I' + str(root / 'protocol/local'), '-I' + str(messages),
        '--python_out=' + str(python), *[str(messages / name) for name in ['studio.proto', 'meta.proto', 'core.proto', 'behaviors.proto', 'keymap.proto']],
        str(root / 'protocol/local/sensors.proto')], check=True)
    return library

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nanopb', type=Path, required=True)
    parser.add_argument('--messages', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'build/host')
    args = parser.parse_args()
    print(build(args.nanopb.resolve(), args.messages.resolve(), args.output.resolve()))
