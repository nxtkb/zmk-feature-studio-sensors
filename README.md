# ZMK Studio Sensors

Runtime editing of per-layer sensor bindings for ZMK keyboards. This module
adds discovery, validated binding changes and persistence through Studio's
Save/Discard/Reset operations. Encoders are the first supported sensor type.

This is the **NXTKB protocol extension 0.1**, not an upstream ZMK Studio feature.
The wire identifier remains `com.nxtkb.studio.sensors.local/0.1` for compatibility
with existing clients. A compatible client is required; the stock ZMK Studio UI
does not provide an encoder editor. The companion NXTKB App is not yet released.

## Install

Start with a working Studio-enabled keyboard configuration. Add this project to
its existing west manifest, using an NXTKB remote:

```yaml
manifest:
  remotes:
    - name: nxtkb
      url-base: https://github.com/nxtkb
  projects:
    - name: zmk-feature-studio-sensors
      remote: nxtkb
      revision: v0.1.0
```

Merge these entries into your manifest; retain your existing ZMK and other
projects. Supported baselines are ZMK
`4de313367288aad5aeaa1243890e40dc813f7008` and zmk-studio-messages
`6cb4c283e76209d59c45fbcb218800cd19e9339d`.

From the west workspace, update projects and apply the module-owned patches:

```sh
west update
west patch -sm zmk-feature-studio-sensors apply
```

Add `-S studio-sensors-local` to the keyboard's normal build command. The snippet
explicitly enables the extension; adding the module alone does not enable it.
The keyboard's existing display, USB and other patches remain separate.

## Describe your hardware

The backend discovers keymap sensors and exposes stable IDs, names, types and
optional press-key associations. Clients enumerate descriptors rather than
assuming a fixed number of encoders or a particular keyboard model.

See [board integration](docs/integration.md) for optional metadata, slot limits
and behavior support. Sensor parameters retain their native ZMK meaning;
ordinary encoder presses remain normal key bindings.

## Protocol and upgrade behavior

- [Protocol contract](docs/protocol.md) and [canonical schema](protocol/local/sensors.proto)
- [Validation and known limitations](docs/validation.md)
- [Contributor tests](docs/testing.md)
- [Design rationale](docs/rfcs/0001-studio-sensors.md)

Settings include a schema version, firmware compatibility signature and CRC.
An incompatible firmware uses stock sensor bindings and reports unsaved changes;
it retains the old records until Save or Reset. Save replaces them with the
current values. Back up custom sensor bindings before changing firmware: automatic
cross-firmware migration is not provided in 0.1.

Persistence is best-effort, not a whole-keymap atomic transaction. After a write
or Save times out, read the actual state before deciding what to do; do not
blindly repeat configuration writes. See the validation notes for Bluetooth
re-pairing and host-specific Consumer HID limitations.

MIT. Integration patches retain upstream copyright headers. Any future upstream
contribution must follow [ZMK's contribution policy](https://zmk.dev/docs/development/contributing/clean-room).
