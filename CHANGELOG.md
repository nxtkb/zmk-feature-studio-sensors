# Changelog

## 0.1.0

- Initial public NXTKB Studio sensor-binding extension, with descriptor-driven
  discovery, behavior contexts, parameter roles and conflict tokens.
- Shared Studio Save/Discard/Reset, CRC-checked records and conservative firmware
  compatibility checks covering west and explicit modules.
- Pinned ZMK/messages integration and independent Studio transport repairs,
  including framing backpressure, error-path lock release and active-profile RX.
- Portable C/Nanopb tests and board integration documentation.

The wire extension is not allocated upstream. The companion NXTKB App is not yet
released. See [validation and limitations](docs/validation.md) before integrating.
