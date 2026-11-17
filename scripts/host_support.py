"""Test-only transport/catalog around the production C sensor engine and codec."""
from pathlib import Path
import ctypes
import sys

BUILD = Path(__file__).resolve().parents[1] / 'build/host'
sys.path.insert(0, str(BUILD / 'python'))
import sensors_pb2 as S
import studio_pb2 as Studio
import keymap_pb2 as K
import behaviors_pb2 as B
import core_pb2 as Core

class Backend:
    def __init__(self, count=3):
        suffix = 'dylib' if sys.platform == 'darwin' else 'so'
        self.lib = ctypes.CDLL(str(BUILD / f'libstudio_sensors.{suffix}'))
        self.lib.ss_host_boot.argtypes = [ctypes.c_uint, ctypes.c_bool]
        self.lib.ss_host_call.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
        assert self.lib.ss_host_boot(count, True) == 0
        self.count = count
        self.locked = False

    def call(self, request):
        data = request.SerializeToString(); out = ctypes.create_string_buffer(65536)
        n = self.lib.ss_host_call(data, len(data), out, len(out))
        if n < 0: raise ValueError(f'C codec failed: {n}')
        return S.Response.FromString(out.raw[:n])

    def describe(self):
        return self.call(S.Request(describe=S.DescribeRequest())).described

    def read(self, layer=0, token=None):
        return self.call(S.Request(read_layer=S.ReadLayerRequest(layer_id=layer,
            expected_state=token or self.describe().state)))

    def set(self, sensor=0, layer=0, value=None, token=None, inherit=False):
        binding = S.SensorBinding(sensor_id=sensor)
        if inherit: binding.inherit.SetInParent()
        else: binding.behavior.CopyFrom(value or S.BehaviorBinding(behavior_id=42, param1=0xC00E2, param2=0xC00E9))
        return self.call(S.Request(set_binding=S.SetBindingRequest(layer_id=layer,
            binding=binding, expected_state=token or self.describe().state)))

    def envelope(self, raw):
        request = Studio.Request.FromString(raw)
        response = Studio.Response(); out = response.request_response; out.request_id = request.request_id
        if request.HasField('core'):
            if request.core.WhichOneof('request_type') == 'get_device_info':
                out.core.get_device_info.name = 'Local sensor protocol fixture'
            elif request.core.WhichOneof('request_type') == 'get_lock_state':
                out.core.get_lock_state = Core.ZMK_STUDIO_CORE_LOCK_STATE_LOCKED if self.locked else Core.ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED
            elif request.core.WhichOneof('request_type') == 'reset_settings':
                out.core.reset_settings = self.lib.ss_host_reset() == 0
            else: out.meta.simple_error = 2
        elif request.HasField('keymap'):
            operation = request.keymap.WhichOneof('request_type')
            if self.locked: out.meta.simple_error = 1
            elif operation == 'sensors': out.keymap.sensors.CopyFrom(self.call(request.keymap.sensors))
            elif operation == 'get_keymap':
                for id, name in [(0, 'MAC'), (8, 'OTHER')]:
                    layer = out.keymap.get_keymap.layers.add(id=id, name=name)
                    for i in range(36): layer.bindings.add(behavior_id=7, param1=0x70004 + i % 26)
            elif operation == 'get_physical_layouts':
                layout = out.keymap.get_physical_layouts.layouts.add(name='Generic fixture')
                for i in range(36): layout.keys.add(width=100, height=100, x=(i % 6) * 100, y=(i // 6) * 100)
            elif operation == 'save_changes':
                ret = self.lib.ss_host_save()
                if ret: out.keymap.save_changes.err = K.SAVE_CHANGES_ERR_NO_SPACE
                else: out.keymap.save_changes.ok = True
            elif operation == 'discard_changes':
                out.keymap.discard_changes = self.lib.ss_host_discard() == 0
            elif operation == 'set_layer_binding':
                self.lib.ss_host_ordinary_change(); out.keymap.set_layer_binding = 0
            elif operation == 'check_unsaved_changes':
                out.keymap.check_unsaved_changes = self.read().layer.has_unsaved_changes
            else: out.meta.simple_error = 2
        elif request.HasField('behaviors'):
            operation = request.behaviors.WhichOneof('request_type')
            if operation == 'list_all_behaviors': out.behaviors.list_all_behaviors.behaviors.extend([7, 42, 43, 44])
            else:
                id = request.behaviors.get_behavior_details.behavior_id
                detail = out.behaviors.get_behavior_details; detail.id = id
                detail.display_name = {7: 'Key Press', 42: 'Rotate Key Press', 43: 'Custom dial mode', 44: 'Dual context example'}[id]
                metadata = detail.metadata.add()
                if id in (7, 42):
                    metadata.param1.add().hid_usage.CopyFrom(B.BehaviorParameterHidUsage(keyboard_max=255, consumer_max=0xFFF))
                    if id == 42: metadata.param2.add().hid_usage.CopyFrom(B.BehaviorParameterHidUsage(keyboard_max=255, consumer_max=0xFFF))
                else:
                    for value in range(3 if id == 43 else 5): metadata.param1.add(name=f'Mode {value}', constant=value)
        else: out.meta.simple_error = 2
        return response.SerializeToString()

def encode(raw):
    output = bytearray([0xAB])
    for byte in raw:
        if byte in (0xAB, 0xAC, 0xAD): output.append(0xAC)
        output.append(byte)
    output.append(0xAD)
    return bytes(output)

def frames(stream):
    data = None; escaped = False
    while True:
        value = stream.read(1)
        if not value: return
        byte = value[0]
        if data is None:
            if byte == 0xAB: data = bytearray()
        elif escaped:
            data.append(byte); escaped = False
        elif byte == 0xAC: escaped = True
        elif byte == 0xAD:
            yield bytes(data); data = None
        elif byte == 0xAB: data.clear()
        else: data.append(byte)
