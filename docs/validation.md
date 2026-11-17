# Validation and limitations

The 0.1 implementation has been tested with official ZMK `4de31336` and messages
`6cb4c28`. The protocol is an NXTKB extension, not an upstream allocation.

## Evidence

- Production C/Nanopb host tests cover 0/1/3 sensors, ID/press position 0,
  stable-ID reordering, inheritance, invalid parameters, stale tokens, persistence,
  corruption, compatibility rejection and concurrent event reads.
- Actual Android clients have exercised a three-sensor C backend and a new
  metadata-defined behavior without keyboard-model branches.
- Sweep Pro left-with-display hardware has exercised live encoder A/B input,
  remote readback, Discard, Save and a real USB-disconnected battery power cycle.
  Original ordinary keys and all sensor bindings were restored and compared.
- A zero-sensor Ferris Sweep configuration has built with the same module.
- See [contributor tests](testing.md) for current automated coverage. Compile
  success does not qualify an untested board or sensor driver.

## Known boundaries

- No whole-keymap atomic Save or automatic cross-firmware migration. Back up
  custom sensor bindings before changing firmware; see the [protocol](protocol.md).
- New token notifications are not wired; conflict detection relies on expected
  tokens. Other clients' existing Save/Discard operate on the shared edit state.
- Bluetooth re-pairing or a Save followed immediately by a full read has sometimes
  led to a Studio timeout in device trials. Read-only reconnect recovered some
  cases; a keyboard restart recovered another. This lifecycle issue remains under
  investigation. Do not automatically retry configuration writes after a timeout.
- Consumer HID host compatibility is separate from sensor editing. In a Sweep Pro
  trial on one Honor PPG-AN00/Android 16, fresh-paired Full reports typed letters
  but volume did not respond; fresh-paired Basic reports worked. This does not
  establish a result for every host or firmware. Full stays the keyboard default;
  see [the official compatibility guide](https://nxtkb.com/docs/setup/connect/#bluetooth-media-key-compatibility).
- New hardware types, variable-length behavior parameters and other clients need
  their own interoperability validation. The stock Studio UI has no sensor editor.
