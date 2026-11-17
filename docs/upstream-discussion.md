# Discussion draft: runtime editing of keymap sensor bindings

Status: historical local discussion draft; not posted. No message field allocation is assumed.
Self-use now takes priority; see [local experiment 0.1](local-trial.md) for the
implemented replacement and remaining validation before any future submission.

We would like to contribute runtime editing of keymap sensor bindings to Studio,
starting with relative rotary encoders and retaining ZMK's existing one sensor
behavior per layer semantics. The implementation should work for arbitrary boards
and clients, independently of our keyboard model or Android application.

We have an early two-patch prototype against ZMK 4de31336 / messages 6cb4c28. It
demonstrates binding reads/writes through existing Studio transport, but is not a
proposed stable protocol: its tag allocation, index-based persistence and boolean
behavior classification need replacement. It is excluded from our default release.

The proposed contribution has three separable parts:

1. A generic keymap sensor backend: enumeration, validated binding read/write,
   identity/topology checks and shared keymap save/discard/reset lifecycle hooks.
2. Behavior capability metadata describing supported binding contexts, with
   parameter roles kept separate from parameter value constraints. A behavior may
   support both key and sensor bindings.
3. Studio messages and client integration: explicit describe/version/operations,
   ID-addressed sensor bindings, inherited bindings and state conflict handling.

We propose retaining the Studio transports and normal physical unlock checks.
Sensor discovery would describe a variable number of sensors and optional ordinary
press-key associations, without encoding left/right placement or a particular
encoder driver into the protocol. Save would initially retain the existing
best-effort guarantee, rather than claiming whole-keymap crash atomicity.

Questions for maintainers before assigning wire fields:

- Should sensor operations live directly in the keymap subsystem, or behind a
  typed nested request using a separate sensors.proto?
- What is the preferred scope for capability/version discovery?
- Should binding contexts and parameter presentation roles extend the existing
  behavior metadata, or be separate additive descriptions?
- What is the preferred shared keymap lifecycle and revision notification API for
  modules? Which storage/migration guarantees should be required initially?

The module would initially contain small, pinned integration patches because the
current protobuf envelope does not provide arbitrary module message registration.
Our goal is to upstream the generic API and agreed schema, then remove those patches.
We do not require a general-purpose extension bus to implement this feature.

Planned qualification includes multiple board configurations, zero/one/multiple
encoders, old clients and firmware, dual-context and custom behaviors, lock checks,
conflicts, injected storage failures and reboot/upgrade migration. Existing Android
mock/UI tests and successful firmware builds do not establish those guarantees.

Implementation and contributions will follow ZMK's clean-room policy and retain MIT
licensing. Protocol definitions, firmware backend and client UI can be reviewed in
separate changes, following the upstream contribution guidance.
