# Pinned Studio transmit compatibility

2026-11-17. This is a local compatibility patch against official ZMK
`4de313367288aad5aeaa1243890e40dc813f7008`; it is not an upstream submission.
The patch is independent of sensor identities and does not change the 0.1 schema.

## Observed failure

On the actual Sweep Pro / Honor Android 16 BLE connection, a read-layer response
delivered 91 bytes: SOF plus the complete 90-byte protobuf payload, without EOF.
The App correctly kept waiting for a complete frame and then timed out. This was
reproduced during read-back of temporary sensor bindings, independently of HID
typing; normal Q and eight encoder-generated A press/release pairs were captured
by Android's input subsystem in the same trial.

The pinned `send_response()` ignores the return values of the one-byte
`ring_buf_put()` calls for SOF and EOF. With the default 64-byte TX ring and a
27-byte in-flight BLE indication, encoding a 90-byte payload can leave the ring
exactly full at EOF. That byte is discarded. This is not a missing sensor binding
or a reason to accept an incomplete frame on the client.

## Local repair

- Enqueue both delimiters with backpressure, waiting until one byte is accepted.
- Yield briefly when the payload ring is full so the transport worker can drain it.
- Bound BLE's flush threshold by its actual 27-byte indication buffer, rather than
  a possibly larger link-layer data length; schedule work when the threshold is met.

No HID descriptor, keymap, behavior semantics, framing bytes or protobuf fields
are changed. The repair does not claim reliable retransmission across disconnects,
or solve every transport error path in the pinned code.

## Reproducible test

```sh
python scripts/test_studio_tx.py --zmk /path/to/pinned-patched-zmk
```

The test compiles the actual transmit functions extracted from the supplied
checkout, with a host port modeling the bounded ring and asynchronous draining.
It also compiles the original pinned functions from Git HEAD as a negative
control: the 90-byte response deterministically produces 91 bytes instead of 92.
The patched functions cover exact-full EOF, initially full-ring SOF, all escaped
byte values, payload sizes 0–1024, several ring capacities and the BLE flush
threshold. This tests production C functions, not a reimplementation of them.
Actual BLE hardware retesting remains required after rebuilding and flashing.
