# Contributor tests

Host tests compile the production C engine and Nanopb adapter; a small port supplies
catalog, transport, storage failures and locking. They do not replace device tests.
Install `tests/requirements.txt`, a C compiler, CMake and protoc. Obtain the pinned
ZMK/messages baselines from the README and Nanopb commit
`7307ce399b81ddcb3c3a5dc862c52d4754328d38`.

```sh
python scripts/check_patches.py --zmk /path/to/zmk --messages /path/to/messages
python scripts/test_storage_signature.py
```

Apply this module's patches to isolated checkouts, then run:

```sh
python scripts/test_studio_tx.py --zmk /path/to/patched-zmk
python scripts/build_host.py --nanopb /path/to/nanopb \
    --messages /path/to/patched-messages/proto/zmk
python scripts/test_runtime.py
```

The storage test exercises the production CMake helper with west-only/explicit
modules, duplicate paths, moved workspaces and incremental invalidation.
The transport port compiles actual pinned/patched C functions, including negative
controls for lost EOF, saturated RX, an encoding-error lock leak and inactive
Bluetooth-profile writes.

To regenerate patches after editing the pinned worktrees:

```sh
python scripts/export_patches.py --zmk /path/to/patched-zmk \
    --messages /path/to/patched-messages
```

`check_patches.py` checks hashes, allowed file scope, baseline applicability and
canonical schema identity without modifying the supplied checkouts.
