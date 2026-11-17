# Protocol 0.1

The canonical schema is `protocol/local/sensors.proto`, generated independently
of the existing Studio keymap/behavior schema. This is an NXTKB extension using
private keymap request/response field **101**; upstream has not allocated this
field. Combining extensions requires checking for field collisions.

## Discovery and addressing

A client must be connected and physically Studio-unlocked. Send Describe, verify
`com.nxtkb.studio.sensors.local/0.1` and major/minor 0/1, then use the returned
operations, descriptors, contexts, parameter metadata and limits.
An empty sensor list is valid. An unknown type or unsupported version must not
become editable. A timeout is an unknown outcome, not proof of no support.

Use stable layer IDs and explicit sensor IDs for every operation. Descriptor
order is presentation only. ID 0 and press position 0 use presence fields;
absence and zero have different meanings.

## Reads, writes and conflicts

ReadLayer and SetBinding require the expected generation/topology/revision token.
The device compares a write token, validates the complete native binding and
commits it under the engine lock. Stale tokens fail without changing bindings.
An inherited binding is explicit, distinct from an explicit no-op behavior.

Read responses use a stable row snapshot while Nanopb computes size and encodes.
The RPC adapter is single-flight: serialize handling plus encoding, because its
response callbacks share storage. The portable engine protects event reads and
mutation through its supplied lock callbacks.

The boot generation changes on restart. Topology includes firmware compatibility
and the selected physical layout. Sensor writes and observed ordinary keymap
mutations advance revision. There is no whole-keymap linearizable transaction
or new token-change notification; rediscover/read after reconnect or conflict.
Queued input actions may finish after a binding change.

## Persistence

Use existing Studio Save/Discard/Reset, shared with ordinary key editing.
Save is best-effort per record; partial failure is not a rollback. A concurrent
new edit remains dirty rather than being falsely reported saved. Discard restores
saved bindings and fails if incompatible records prevent a safe restore.
Reset removes sensor records and restores firmware stock bindings.

Records contain schema 1, a 16-byte compatibility signature and CRC. Invalid or
incompatible records are retained but not executed; stock is used and the state
remains dirty until an explicit Save or Reset. Save writes the current values.
Automatic migration across firmware builds or the obsolete tag-100 prototype
is not supported. Back up custom bindings before upgrading.

After a timeout, reconnect/read to determine the actual outcome before writing
again. A positive Save ACK followed by a failed read can still mean Save succeeded.
