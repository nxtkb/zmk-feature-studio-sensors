#!/usr/bin/env python3
"""Exercise the production CMake signature with west and explicit modules."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def fixture(path):
    for name, text in {
        'app/src/behaviors/key.c': 'int key_behavior = 1;\n',
        'west/src/sensor.c': 'int sensor_behavior = 1;\n',
        'extra/include/behavior.h': '#define BEHAVIOR_VERSION 1\n',
        'zephyr.dts': '/ { model = "signature-test"; };\n',
        'autoconf.h': '#define CONFIG_SENSOR 1\n',
    }.items():
        file = path / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(text)
    (path / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.20)
project(signature NONE)
set(APPLICATION_SOURCE_DIR "${{CMAKE_CURRENT_SOURCE_DIR}}/app")
file(MAKE_DIRECTORY "${{PROJECT_BINARY_DIR}}/include/generated/zephyr")
configure_file(zephyr.dts zephyr.dts COPYONLY)
configure_file(autoconf.h include/generated/zephyr/autoconf.h COPYONLY)
set(ZEPHYR_MODULE_NAMES west-behavior)
set(ZEPHYR_WEST_BEHAVIOR_MODULE_DIR "${{CMAKE_CURRENT_SOURCE_DIR}}/west")
set(ZMK_EXTRA_MODULES "${{CMAKE_CURRENT_SOURCE_DIR}}/extra")
if(WEST_ONLY)
  set(ZEPHYR_MODULE_NAMES extra-behavior west-behavior)
  set(ZEPHYR_EXTRA_BEHAVIOR_MODULE_DIR "${{CMAKE_CURRENT_SOURCE_DIR}}/extra")
  set(ZMK_EXTRA_MODULES "")
endif()
if(DUPLICATE_MODULE)
  list(APPEND ZMK_EXTRA_MODULES "${{CMAKE_CURRENT_SOURCE_DIR}}/west")
endif()
include("{ROOT}/cmake/storage-signature.cmake")
ss_storage_signature(signature)
file(WRITE "${{PROJECT_BINARY_DIR}}/signature.txt" "${{signature}}")
''')


def configure(path, *args):
    subprocess.run(['cmake', '-S', str(path), '-B', str(path / 'build'), *args],
                   check=True, stdout=subprocess.DEVNULL)
    return (path / 'build/signature.txt').read_text()


def main():
    with tempfile.TemporaryDirectory(prefix='sensor-signature-') as temporary:
        a, b = Path(temporary) / 'a', Path(temporary) / 'different-workspace'
        fixture(a)
        fixture(b)
        original = configure(a)
        assert original == configure(b), 'Workspace path changed compatibility'
        assert original == configure(b, '-DWEST_ONLY=ON'), 'West discovery changed compatibility'
        assert original == configure(a, '-DDUPLICATE_MODULE=ON'), 'Duplicate module changed compatibility'
        for name in ['west/src/sensor.c', 'extra/include/behavior.h', 'app/src/behaviors/key.c',
                     'autoconf.h', 'zephyr.dts']:
            file = b / name
            before = file.read_text()
            file.write_text(before + '// changed\n')
            assert configure(b) != original, f'{name} was omitted from compatibility'
            file.write_text(before)
        assert configure(b) == original
        (b / 'west/src/sensor.c').write_text('int sensor_behavior = 2;\n')
        subprocess.run(['cmake', '--build', str(b / 'build')], check=True, stdout=subprocess.DEVNULL)
        assert (b / 'build/signature.txt').read_text() != original, 'Incremental build missed module change'
    print('PASS: west/explicit modules, duplicate paths, relocated workspace and incremental invalidation')


if __name__ == '__main__':
    main()
