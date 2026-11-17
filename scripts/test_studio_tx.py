#!/usr/bin/env python3
"""Compile actual Studio TX/RX functions with a bounded-ring host port.

Also run the unpatched pinned function as a negative control: its 90-byte
payload loses EOF with a 64-byte ring and a 27-byte in-flight indication.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def compile_port(folder, rpc, gatt):
    names = ['static bool rpc_tx_buffer_write(', 'static pb_ostream_t pb_ostream_for_tx_buf(',
             'static void rpc_tx_framing_byte_write(', 'static int send_response(']
    (folder / 'native_tx.inc').write_text(''.join(function(rpc, name) for name in names if name in rpc))
    constants = '\n'.join(re.findall(r'^#define RPC_INDICATION_BUFFER_SIZE .+$', gatt, re.M))
    (folder / 'native_gatt.inc').write_text(constants + '\n' + function(gatt, 'static void refresh_notify_size(') +
                                         function(gatt, 'static void gatt_tx_notify(') +
                                         function(gatt, 'static ssize_t write_rpc_req('))
    output = folder / 'studio_tx'
    subprocess.run([shutil.which('cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-implicit-fallthrough',
                    '-I' + str(folder), str(ROOT / 'tests/studio_tx_port.c'),
                    '-o', str(output)], check=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zmk', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='studio-tx-test-') as temporary:
        folder = Path(temporary)
        paths = ['app/src/studio/rpc.c', 'app/src/studio/gatt_rpc_transport.c']
        baseline = [subprocess.check_output(['git', '-C', str(args.zmk), 'show', 'HEAD:' + path], text=True) for path in paths]
        old = compile_port(folder, *baseline)
        result = subprocess.run([str(old), '--regression-only'], capture_output=True, text=True, timeout=5)
        if result.returncode != 2 or 'bytes=91 expected=92' not in result.stderr:
            raise RuntimeError('The pinned negative control did not reproduce the observed missing EOF: ' + result.stderr)
        print('PASS negative control: pinned ZMK drops EOF (91 bytes instead of 92)')
        rx = subprocess.run([str(old), '--rx-regression-only'], capture_output=True, text=True, timeout=5)
        if rx.returncode != 3 or 'RX producer spins' not in rx.stderr:
            raise RuntimeError('The pinned RX negative control did not reproduce the stalled producer: ' + rx.stderr)
        print('PASS negative control: pinned saturated RX producer starves decoder')
        for flag, code, message in [
            ('--encode-failure-regression-only', 4, 'Encoding failure retained transport mutex'),
            ('--ownership-regression-only', 5, 'Inactive profile request was accepted'),
        ]:
            result = subprocess.run([str(old), flag], capture_output=True, text=True, timeout=5)
            if result.returncode != code or message not in result.stderr:
                raise RuntimeError('Pinned negative control did not reproduce: ' + message)
            print('PASS negative control: ' + message)
        patched = compile_port(folder, *[(args.zmk / path).read_text() for path in paths])
        subprocess.run([str(patched)], check=True, timeout=15)


if __name__ == '__main__':
    main()
