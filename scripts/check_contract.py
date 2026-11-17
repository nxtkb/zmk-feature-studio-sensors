#!/usr/bin/env python3
"""Check the candidate schema, without modifying or enrolling a Studio envelope.

These tests verify wire representation, not firmware or client implementation.
Requires protoc and google.protobuf. All generated descriptors are temporary.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from google.protobuf import descriptor_pb2, descriptor_pool, message_factory


def messages(upstream: Path, protoc: str):
    schema = Path(__file__).resolve().parents[1] / "protocol" / "draft" / "sensors.proto"
    with tempfile.TemporaryDirectory(prefix="studio-sensors-schema-") as temporary:
        target = Path(temporary) / "schema.pb"
        subprocess.run(
            [protoc, f"-I{schema.parent}", f"-I{upstream}", "--include_imports",
             f"--descriptor_set_out={target}", str(schema)], check=True,
        )
        files = descriptor_pb2.FileDescriptorSet.FromString(target.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for file in files.file:
        pool.Add(file)
    return lambda name: message_factory.GetMessageClass(
        pool.FindMessageTypeByName("zmk.sensors." + name)
    )


class ContractTest(unittest.TestCase):
    types = None

    def msg(self, name, **fields):
        return self.types(name)(**fields)

    def test_missing_identifier_is_distinct_from_explicit_zero(self):
        request = self.msg("SetBindingRequest")
        self.assertFalse(request.HasField("layer_id"))
        request.layer_id = 0
        request.binding.sensor_id = 0
        restored = self.types("SetBindingRequest").FromString(request.SerializeToString())
        self.assertTrue(restored.HasField("layer_id"))
        self.assertTrue(restored.binding.HasField("sensor_id"))
        self.assertEqual(restored.layer_id, 0)
        self.assertEqual(restored.binding.sensor_id, 0)

    def test_press_position_zero_is_not_no_press_button(self):
        sensor = self.msg("SensorDescriptor", sensor_id=17)
        self.assertFalse(sensor.HasField("press_key_position"))
        sensor.press_key_position = 0
        restored = self.types("SensorDescriptor").FromString(sensor.SerializeToString())
        self.assertTrue(restored.HasField("press_key_position"))
        self.assertEqual(restored.press_key_position, 0)

    def test_zero_one_and_multiple_sensors_have_no_two_sensor_assumption(self):
        for count in (0, 1, 3):
            service = self.msg("ServiceInfo", protocol_id="draft-test")
            service.version.major = 1
            for i in range(count):
                service.sensors.add(sensor_id=100 + i * 9, display_name=f"Dial {i}")
            restored = self.types("ServiceInfo").FromString(service.SerializeToString())
            self.assertEqual(len(restored.sensors), count)
            self.assertTrue(restored.HasField("version"))

    def test_sensor_identity_is_not_descriptor_position(self):
        first = self.msg("ServiceInfo")
        first.sensors.add(sensor_id=73, stable_key="main-dial")
        first.sensors.add(sensor_id=4, stable_key="aux-dial")
        reordered = self.msg("ServiceInfo")
        reordered.sensors.extend(reversed(first.sensors))
        write = self.msg("SetBindingRequest", layer_id=0)
        write.binding.sensor_id = 73
        self.assertEqual(reordered.sensors[0].sensor_id, 4)
        by_id = {sensor.sensor_id: sensor for sensor in reordered.sensors}
        self.assertEqual(by_id[write.binding.sensor_id].stable_key, "main-dial")

    def test_inherit_empty_and_behavior_are_distinct(self):
        binding = self.msg("SensorBinding", sensor_id=1)
        self.assertIsNone(binding.WhichOneof("value"))
        binding.inherit.SetInParent()
        self.assertEqual(binding.WhichOneof("value"), "inherit")
        binding.behavior.behavior_id = 3
        self.assertEqual(binding.WhichOneof("value"), "behavior")
        self.assertFalse(binding.HasField("inherit"))

    def test_behavior_can_support_both_contexts_without_direction_roles(self):
        behavior = self.msg("BehaviorSupport", behavior_id=8, contexts=[1, 2])
        restored = self.types("BehaviorSupport").FromString(behavior.SerializeToString())
        self.assertEqual(list(restored.contexts), [1, 2])
        self.assertEqual(len(restored.parameters), 0)

    def test_unknown_kind_and_fields_are_preserved(self):
        sensor = self.msg("SensorDescriptor", sensor_id=8, kind=77)
        # Unknown field 120, varint value 9. Existing fields must not erase it.
        raw = sensor.SerializeToString() + bytes([0xC0, 0x07, 0x09])
        restored = self.types("SensorDescriptor").FromString(raw)
        self.assertEqual(restored.kind, 77)
        self.assertIn(bytes([0xC0, 0x07, 0x09]), restored.SerializeToString())

    def test_result_presence_not_default_zero_means_success(self):
        response = self.msg("Response")
        self.assertIsNone(response.WhichOneof("result"))
        response.error.code = 7
        self.assertEqual(response.WhichOneof("result"), "error")
        response.binding.SetInParent()
        self.assertEqual(response.WhichOneof("result"), "binding")
        self.assertFalse(response.HasField("error"))

    def test_explicit_state_token_preserves_zero_revision(self):
        request = self.msg("SetBindingRequest", layer_id=0)
        request.expected_state.generation = b"g" * 16
        request.expected_state.topology = b"t" * 16
        request.expected_state.revision = 0
        restored = self.types("SetBindingRequest").FromString(request.SerializeToString())
        self.assertTrue(restored.HasField("expected_state"))
        self.assertEqual(restored.expected_state.revision, 0)
        self.assertEqual(restored.expected_state.generation, b"g" * 16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream-proto", type=Path, required=True)
    parser.add_argument("--protoc", default=shutil.which("protoc"))
    args = parser.parse_args()
    if not args.protoc or not (args.upstream_proto / "keymap.proto").is_file():
        parser.error("Provide protoc and the upstream proto/zmk directory containing keymap.proto")
    factory = messages(args.upstream_proto.resolve(), args.protoc)
    # Avoid Python binding the function as a method on test instances.
    ContractTest.types = staticmethod(factory)
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ContractTest))
    raise SystemExit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
