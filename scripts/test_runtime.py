#!/usr/bin/env python3
"""Exercise the compiled firmware engine + Nanopb codec, not a Python model."""
import unittest
from host_support import Backend, S

class RuntimeTest(unittest.TestCase):
    def setUp(self): self.backend = Backend()

    def test_zero_one_three_and_explicit_zero_identifier(self):
        for count in (0, 1, 3):
            backend = Backend(count); info = backend.describe()
            self.assertEqual(len(info.sensors), count)
            self.assertEqual(info.protocol_id, 'com.nxtkb.studio.sensors.local/0.1')
            self.assertEqual(len(backend.read().layer.bindings), count)
            if count:
                self.assertTrue(info.sensors[0].HasField('sensor_id'))
                self.assertEqual(info.sensors[0].sensor_id, 0)
                self.assertTrue(info.sensors[0].HasField('press_key_position'))
                self.assertEqual(info.sensors[0].press_key_position, 0)

    def test_edit_save_reboot_and_discard(self):
        backend = self.backend
        original = backend.read().layer.bindings[0].behavior
        self.assertTrue(backend.set().binding.has_unsaved_changes)
        backend.lib.ss_host_discard()
        self.assertEqual(backend.read().layer.bindings[0].behavior, original)
        backend.set(); self.assertEqual(backend.lib.ss_host_save(), 0)
        before = backend.describe().state.generation
        self.assertEqual(backend.lib.ss_host_boot(3, False), 0)
        self.assertNotEqual(backend.describe().state.generation, before)
        self.assertEqual(backend.read().layer.bindings[0].behavior.param1, 0xC00E2)
        self.assertFalse(backend.read().layer.has_unsaved_changes)

    def test_stale_tokens_and_missing_ids_never_mutate(self):
        backend = self.backend; token = backend.describe().state
        backend.lib.ss_host_ordinary_change()
        self.assertEqual(backend.set(token=token).error.code, S.STATE_CONFLICT)
        request = S.Request(set_binding=S.SetBindingRequest(expected_state=backend.describe().state))
        self.assertEqual(backend.call(request).error.code, S.INVALID_REQUEST)
        self.assertEqual(backend.read().layer.bindings[0].behavior.param1, 0xC00EA)

    def test_descriptor_reorder_and_noncontiguous_ids(self):
        backend = self.backend; old = backend.describe().state
        backend.lib.ss_host_reorder()
        info = backend.describe()
        self.assertEqual([x.sensor_id for x in info.sensors], [17, 0, 29])
        self.assertEqual(backend.set(sensor=17, token=old).error.code, S.STATE_CONFLICT)
        self.assertTrue(backend.set(sensor=17).HasField('binding'))
        by_id = {x.sensor_id: x for x in backend.read().layer.bindings}
        self.assertEqual(by_id[17].behavior.param1, 0xC00E2)
        self.assertEqual(by_id[0].behavior.param1, 0xC00EA)

    def test_custom_behavior_and_dual_context_metadata(self):
        backend = self.backend; info = backend.describe()
        support = {x.behavior_id: x for x in info.behavior_support}
        self.assertEqual(list(support[44].contexts), [S.KEY, S.SENSOR])
        self.assertEqual(len(support[43].parameters), 0)
        self.assertTrue(backend.set(value=S.BehaviorBinding(behavior_id=43, param1=2)).HasField('binding'))
        self.assertEqual(backend.read().layer.bindings[0].behavior.param1, 2)
        self.assertEqual(backend.set(value=S.BehaviorBinding(behavior_id=7, param1=0x70004)).error.code, S.INVALID_BEHAVIOR)
        self.assertEqual(backend.set(value=S.BehaviorBinding(behavior_id=43, param1=3)).error.code, S.INVALID_PARAMETERS)

    def test_partial_save_is_not_reported_as_atomic(self):
        backend = self.backend
        backend.set(sensor=0); backend.set(sensor=17)
        backend.lib.ss_host_fail_after(1)
        self.assertEqual(backend.lib.ss_host_save(), S.NO_SPACE)
        self.assertTrue(backend.read().layer.has_unsaved_changes)
        backend.lib.ss_host_boot(3, False)
        values = {x.sensor_id: x.behavior.param1 for x in backend.read().layer.bindings}
        self.assertEqual(values[0], 0xC00E2)
        self.assertEqual(values[17], 0xC00EA)

    def test_corrupt_records_are_retained_and_not_applied(self):
        backend = self.backend; backend.set(); backend.lib.ss_host_save(); backend.lib.ss_host_corrupt()
        backend.lib.ss_host_boot(3, False)
        self.assertTrue(backend.describe().storage_incompatible)
        self.assertEqual(backend.read().layer.bindings[0].behavior.param1, 0xC00EA)
        self.assertTrue(backend.read().layer.has_unsaved_changes)
        self.assertEqual(backend.lib.ss_host_discard(), S.PERSISTENCE_FAILED)

    def test_concurrent_event_reads_never_observe_torn_bindings(self):
        self.assertEqual(self.backend.lib.ss_host_stress(), 0)

    def test_change_during_flash_write_remains_dirty(self):
        backend = self.backend; backend.set(); backend.lib.ss_host_change_during_save()
        self.assertEqual(backend.lib.ss_host_save(), S.STATE_CONFLICT)
        self.assertTrue(backend.read().layer.has_unsaved_changes)
        self.assertEqual(backend.read().layer.bindings[0].behavior.param1, 0x70004)
        backend.lib.ss_host_discard()
        self.assertEqual(backend.read().layer.bindings[0].behavior.param1, 0xC00E2)

    def test_firmware_abi_change_never_silently_reuses_old_records(self):
        backend = self.backend; backend.set(); backend.lib.ss_host_save()
        backend.lib.ss_host_abi_change(); backend.lib.ss_host_boot(3, False)
        self.assertTrue(backend.describe().storage_incompatible)
        self.assertEqual(backend.read().layer.bindings[0].behavior.param1, 0xC00EA)

    def test_inherit_is_explicit_and_persistent(self):
        backend = self.backend
        self.assertTrue(backend.set(layer=8, inherit=True).binding.binding.HasField('inherit'))
        backend.lib.ss_host_save(); backend.lib.ss_host_boot(3, False)
        self.assertTrue(backend.read(layer=8).layer.bindings[0].HasField('inherit'))
        self.assertEqual(backend.lib.ss_host_reset(), 0)
        self.assertTrue(backend.read(layer=8).layer.bindings[0].HasField('behavior'))

if __name__ == '__main__': unittest.main(verbosity=2)
