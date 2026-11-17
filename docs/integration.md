# Board integration

Use a Studio-enabled configuration, the pinned baselines in the README, all
patches in `zephyr/patches.yml`, and the `studio-sensors-local` snippet.
The module is independent of display, pointing-device and vendor-HID modules.

## Sensor metadata

The existing keymap `sensors` list and `sensor-bindings` define execution.
Optional metadata provides durable identity and presentation. For a keyboard
with one encoder and no associated press key:

```dts
/ {
    studio_sensor_descriptors {
        compatible = "nxtkb,studio-sensors";
        sensor-ids = <0>;
        sensor-keys = "dial";
        sensor-names = "Dial";
        sensor-kinds = <1>;
        press-positions = <0xffffffff>;
    };
};
```

Each array must match the keymap sensor list's length and internal order.
IDs are unique protocol addresses, not array offsets; ID 0 is valid. Stable keys
are unique, at most 39 ASCII letters/digits or `-_.`, and must remain attached
to the same logical control. Names are at most 63 bytes.

Kind 1 means relative rotary. Unknown kinds are discoverable but read-only.
Press positions refer to the stock physical layout; `0xffffffff` means absent.
The backend translates an association to the selected physical layout.
Without metadata, EC11 sensors receive index-based IDs and generic names, with
no stable key or press association; do not assume cross-topology migration.

## Behavior support

Editable behaviors must implement both sensor callbacks and supply valid Studio
parameter metadata. Native rotate-key-press exposes its two parameters as
clockwise/counterclockwise actions. Other behaviors retain their native parameter
semantics; two parameters alone do not imply two directions.

A behavior may support both ordinary keys and sensors. Parameter types come from
the existing Studio behavior catalog; the extension adds contexts and optional
presentation roles rather than a second behavior registry.

## Resource budget

`CONFIG_NXTKB_STUDIO_SENSORS_MAX_SENSORS` defaults to 4, range 1–16. Set it to the
board's required capacity to avoid allocating unused slots. The build checks
that the actual sensor count fits. Zero-sensor keyboards are supported.

Firmware compatibility signatures include configuration, devicetree, schema,
core behaviors and sources from discovered west/explicit modules. Paths and
module discovery order do not affect the signature. This deliberately favors
rejecting incompatible settings over guessing a migration.
