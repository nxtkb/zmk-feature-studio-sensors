# ZMK Studio Sensors — local experiment 0.1

Runtime editing of per-layer ZMK sensor bindings, packaged as an independent
module with small core/message integration patches. This local implementation
is for self-use and review. **The wire ABI is experimental, not allocated by
upstream, and not enabled in normal NXTKB releases.**

- [Design RFC](docs/rfcs/0001-studio-sensors.md)
- [Local review, validation and trial guide](docs/local-trial.md)
- [Historical tag-100 prototype audit](docs/prototype-audit.md)
- [Unsubmitted upstream discussion draft](docs/upstream-discussion.md)

## What is implemented

The module provides a protobuf-independent C engine, Nanopb RPC adapter, Zephyr
settings integration and a board metadata binding. Discovery reports sensor IDs,
optional stable keys and press associations, types, permissions, behavior contexts,
parameter roles, limits and a generation/topology/revision token. Read and write
address IDs, never display order. Inheritance is explicit. Sensor edits participate
in the existing Studio Save/Discard/Reset lifecycle with **best-effort** persistence.

The executable schema is [protocol/local/sensors.proto](protocol/local/sensors.proto).
The exact handshake is `com.nxtkb.studio.sensors.local/0.1`, major 0, minor 1.
Matching local firmware uses typed keymap request/response **field 101**. This is a
private experiment; a handshake cannot reserve a protobuf field against other
implementations. The Android adapter requires explicit debug opt-in and preserves
pristine official keymap/behavior schemas. Unsupported versions degrade the sensor
feature while keeping ordinary key editing available.

The earlier conceptual schema is retained in `protocol/draft/`. The original
unversioned field-100 patches are archived in `experimental/v0/`; the current App
does not implement that protocol.

## Firmware integration

Pinned baselines:

- ZMK `4de313367288aad5aeaa1243890e40dc813f7008`
- zmk-studio-messages `6cb4c283e76209d59c45fbcb218800cd19e9339d`

Use isolated official checkouts. Apply every patch listed in
`zephyr/patches.yml`, then include the module in `ZMK_EXTRA_MODULES` and explicitly
select the `studio-sensors-local` snippet. Existing USB/display patches and board
build requirements remain owned by their respective modules.

```sh
# From the appropriate west workspace, with the actual module location:
west patch -sm /path/to/zmk-feature-studio-sensors apply
# Add -S studio-sensors-local to the keyboard's normal west build command.
```

`CONFIG_NXTKB_STUDIO_SENSORS_MAX_SENSORS` allocates slots (default 4, range 1–16).
This is a firmware resource budget, not an App assumption about keyboard shape.
The App currently bounds discovery at 64 sensors and 64 KiB per sensor response.
The module can also report an empty sensor list. Optional board metadata uses
`nxtkb,studio-sensors`; see the [trial guide](docs/local-trial.md).

The manifest keeps the sensor-core integration, messages integration and pinned
[Studio buffer backpressure repair](docs/studio-tx-backpressure.md) as separate patches.
The transport repair prevents a full TX ring from dropping response delimiters
and a saturated RX producer from starving its decoder.

## Android trial

From the sibling `nxtkb-app` repository:

```sh
./scripts/android.sh :protocol:test :app:testDebugUnitTest :app:assembleDebug \
    :app:assembleDebugAndroidTest :app:lintDebug \
    -Pnxtkb.experimentalSensorProtocol=true
```

The `protocol` Gradle module reads this repository's `protocol/local` directory.
Ordinary debug and all release/releaseQa builds keep the feature disabled.
This does not qualify the module for release: real keyboard rotation, transport,
power-loss recovery and firmware-upgrade migration still need hardware trials.

## Run the production C backend on a host

Requires a C compiler, protoc, Python protobuf and the pinned Nanopb sources.
Install Python test dependencies with `python -m pip install -r tests/requirements.txt`.
The host port supplies a test-only catalog/transport; the sensor engine and codec
are exactly `src/engine.c` and `src/rpc.c`, shared with firmware.
The RPC adapter is single-flight: callers must serialize handling plus protobuf
encoding; callback response data lasts until the next call. The engine uses its
configured lock to protect event reads and binding mutation.

```sh
python scripts/build_host.py --nanopb /path/to/nanopb \
    --messages /path/to/patched-zmk-studio-messages/proto/zmk
python scripts/test_runtime.py
# Optional phone integration test; listen only on localhost:
python scripts/serve_local.py --port 19761
adb reverse tcp:19761 tcp:19761
adb shell am instrument -w -e class com.nxtkb.app.ui.SensorBackendTest \
    com.nxtkb.app.test/androidx.test.runner.AndroidJUnitRunner
adb reverse --remove tcp:19761
```

CI checks patch checksums, pinned-baseline applicability and schema consistency,
then compiles and tests the production C engine/Nanopb codec and the actual pinned
Studio transmit functions on Linux, including an unpatched negative control. It does
not flash devices or qualify firmware for release. To run patch checks locally:

```sh
python scripts/check_patches.py --zmk /path/to/pinned-zmk-checkout \
    --messages /path/to/pinned-messages-checkout
```

The input checkouts are not modified; patches are applied in temporary directories.

To regenerate the integration patches after editing the pinned worktrees:

```sh
python scripts/export_patches.py --zmk /path/to/pinned-zmk-worktree \
    --messages /path/to/pinned-messages-worktree
```

To reproduce the framing regression and verify the repair against a patched checkout:

```sh
python scripts/test_studio_tx.py --zmk /path/to/pinned-patched-zmk
```

## Limits and licensing

The local storage format validates version, a conservative build signature and
CRC. Changed firmware may reject old sensor settings and use stock bindings;
records are retained, and no automatic cross-firmware migration is promised.
There is no whole-keymap atomic transaction or new state-token notification yet.
Stale writes are rejected; already queued HID actions can finish normally.

MIT. Patches retain ZMK's headers. Any future contribution must follow
[ZMK's contribution policy](https://zmk.dev/docs/development/contributing/clean-room).
Repository: [nxtkb/zmk-feature-studio-sensors](https://github.com/nxtkb/zmk-feature-studio-sensors).
Upstream issues, PRs and discussions have not been submitted. Creating this
repository does not freeze or allocate the experimental protocol.
