# Tag-100 prototype audit

Date: 2026-11-17. Status: development sample; superseded as a protocol design by
[RFC 0001](rfcs/0001-studio-sensors.md). The implementation is retained for comparison in `experimental/v0/`.
This is a historical audit; the current local 0.1 implementation and its limits
are recorded in [the trial review](local-trial.md).

| Finding | Consequence | Required change |
| --- | --- | --- |
| `sensor_count > 0` is treated as capability/version discovery | Zero sensors, read-only support and an incompatible protocol cannot be distinguished | Explicit describe/versions/operations; zero sensors is a valid supported service |
| Tag 100 was chosen in upstream-owned messages without allocation | Other forks or future upstream changes can assign incompatible meanings | Do not publish as a standard; coordinate envelope tags or an extension mechanism with maintainers |
| Sensor index is the runtime and NVS identity | Reordering `sensors` can silently restore a binding to different hardware | Descriptor IDs + topology identity; board-declared durable keys for migration |
| `is_sensor` is a boolean classification | Cannot describe a behavior supporting both ordinary keys and sensors | Supported binding contexts as a set |
| Any pair of HID parameters is displayed as CW/CCW | Custom sensor behaviors may give those parameters other meanings | Driver-declared presentation roles, independent of HID constraints |
| UI identifies left/right and press positions using the known Pro layout | Other boards need product-specific code | Sensor presentation and optional press association reported by firmware |
| Fixed sensor behaviors expose no configurable nested actions | UI cannot change arbitrary per-direction behaviors | Preserve native sensor-binding semantics; separate future runtime behavior editing proposal |
| Empty/inherited sensor binding has no explicit protocol representation | Zero behavior ID can mean absent, inherited, invalid or unknown | An explicit inherited variant; no implicit write defaults |
| Sensor storage is bolted into `keymap.c` and assumes the active topology | Firmware updates lack an intentional migration/invalidation policy | Separate storage/backend API with schema/topology checks and compatibility tests |
| Save loops can fail after earlier records have been committed | A failed save is not an atomic rollback of keys and sensors | Explicit best-effort guarantee; fault injection; do not advertise snapshot atomicity |
| RPC writes and sensor events do not share a defined synchronization contract | A multi-field binding update can race with event consumption | Copy complete bindings under shared protection; invoke behaviors outside it; test queued actions and rotation remainders |
| No revision precondition | A stale UI can overwrite later edits | Generation/topology/revision checks for sensor writes and shared mutation hooks |
| Prototype fields remain in generated App message classes | A runtime opt-in does not by itself restore a pristine upstream decoder schema | Final client adapters must isolate prototype decoding from upstream schema; the current gate blocks activation/writes, not every possible wire collision |
| Two patches were owned by Sweep Pro | Other keyboard projects must depend on a product repository | Independent module and protocol ownership |
| App tests simulate the firmware | They cannot prove lock enforcement, real rotation, reboot persistence or flash recovery | Firmware tests and multi-board hardware qualification before release |

The prototype still demonstrates useful behavior: ordinary key bindings are kept
separate from sensor bindings, firmware checks sensor driver callbacks and
parameters, requests are Studio-secured, and ordinary Studio transport is reused.
Those properties should be retained.

## Containment applied during review

- Patches moved unchanged in behavior into this independent patch-only module.
- Sweep Pro's normal release workflow no longer applies them.
- Android's normal builds do not interpret or write the prototype fields.
  An explicit debug-only opt-in preserves development testing.
- The former "v1" label is retired. Candidate schema fields are design material,
  not a claim of upstream allocation or interoperability.
- Existing prototype binaries remain local historical artifacts; they are not
  release candidates. No keyboard was flashed during this design review.
