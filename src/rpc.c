/* SPDX-License-Identifier: MIT */
#include <pb_encode.h>
#include <string.h>
#include <studio_sensors/rpc.h>

static const struct ss_rpc_port *port;
static struct ss_description description;
static struct ss_binding reply_bindings[SS_MAX_SENSORS];
void ss_rpc_init(const struct ss_rpc_port *p) { port = p; }
static void token_to_wire(zmk_sensors_StateToken *out) {
    memcpy(out->generation, description.token.generation, 16);
    memcpy(out->topology, description.token.topology, 16);
    out->revision = description.token.revision;
}
static struct ss_token token_from_wire(const zmk_sensors_StateToken *in) {
    struct ss_token token;
    memcpy(token.generation, in->generation, 16);
    memcpy(token.topology, in->topology, 16);
    token.revision = in->revision;
    return token;
}
static bool unsaved(void) {
    return ss_unsaved(port->engine) || (port->other_unsaved && port->other_unsaved());
}
static bool encode_sensor(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    for (unsigned i = 0; i < description.sensor_count; i++) {
        const struct ss_sensor *in = &description.sensors[i];
        zmk_sensors_SensorDescriptor out = zmk_sensors_SensorDescriptor_init_zero;
        out.has_sensor_id = true;
        out.sensor_id = in->id;
        out.has_stable_key = in->key[0] != 0;
        strncpy(out.stable_key, in->key, sizeof(out.stable_key) - 1);
        strncpy(out.display_name, in->name, sizeof(out.display_name) - 1);
        out.kind = (zmk_sensors_SensorKind)in->kind;
        out.has_press_key_position = in->has_press;
        out.press_key_position = in->press_position;
        out.writable = in->writable;
        out.supports_inherit = in->writable;
        if (!pb_encode_tag_for_field(stream, field) ||
            !pb_encode_submessage(stream, &zmk_sensors_SensorDescriptor_msg, &out))
            return false;
    }
    return true;
}
static bool encode_behaviors(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    unsigned count = port->behavior_count ? port->behavior_count() : 0;
    for (unsigned i = 0; i < count; i++) {
        struct ss_behavior in = {0};
        if (!port->behavior_at(i, &in) || in.id <= 0)
            continue;
        zmk_sensors_BehaviorSupport out = zmk_sensors_BehaviorSupport_init_zero;
        out.behavior_id = in.id;
        if (in.key)
            out.contexts[out.contexts_count++] = zmk_sensors_BindingContext_KEY;
        if (in.sensor)
            out.contexts[out.contexts_count++] = zmk_sensors_BindingContext_SENSOR;
        if (in.param1_role)
            out.parameters[out.parameters_count++] = (zmk_sensors_ParameterPresentation){
                .parameter_number = 1, .role = (zmk_sensors_ParameterRole)in.param1_role};
        if (in.param2_role)
            out.parameters[out.parameters_count++] = (zmk_sensors_ParameterPresentation){
                .parameter_number = 2, .role = (zmk_sensors_ParameterRole)in.param2_role};
        if (!pb_encode_tag_for_field(stream, field) ||
            !pb_encode_submessage(stream, &zmk_sensors_BehaviorSupport_msg, &out))
            return false;
    }
    return true;
}
static void binding_to_wire(uint32_t id, const struct ss_binding *in,
                            zmk_sensors_SensorBinding *out) {
    *out = (zmk_sensors_SensorBinding)zmk_sensors_SensorBinding_init_zero;
    out->has_sensor_id = true;
    out->sensor_id = id;
    if (in->inherit)
        out->which_value = zmk_sensors_SensorBinding_inherit_tag;
    else {
        out->which_value = zmk_sensors_SensorBinding_behavior_tag;
        out->value.behavior = (zmk_sensors_BehaviorBinding){
            .behavior_id = in->behavior_id, .param1 = in->param1, .param2 = in->param2};
    }
}
static bool encode_bindings(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    for (unsigned i = 0; i < description.sensor_count; i++) {
        struct ss_binding binding = reply_bindings[i];
        uint32_t id = description.sensors[i].id;
        zmk_sensors_SensorBinding out;
        binding_to_wire(id, &binding, &out);
        if (!pb_encode_tag_for_field(stream, field) ||
            !pb_encode_submessage(stream, &zmk_sensors_SensorBinding_msg, &out))
            return false;
    }
    return true;
}
static void error(zmk_sensors_Response *response, int code) {
    *response = (zmk_sensors_Response)zmk_sensors_Response_init_zero;
    response->which_result = zmk_sensors_Response_error_tag;
    response->result.error.code = (zmk_sensors_ErrorCode)code;
}
void ss_rpc_handle(const zmk_sensors_Request *request, zmk_sensors_Response *response) {
    *response = (zmk_sensors_Response)zmk_sensors_Response_init_zero;
    if (!port || !port->engine) {
        error(response, SS_NOT_SUPPORTED);
        return;
    }
    ss_describe(port->engine, &description);
    switch (request->which_request) {
    case zmk_sensors_Request_describe_tag: {
        response->which_result = zmk_sensors_Response_described_tag;
        zmk_sensors_ServiceInfo *out = &response->result.described;
        strcpy(out->protocol_id, SS_PROTOCOL_ID);
        out->has_version = true;
        out->version.major = 0;
        out->version.minor = 1;
        out->operations_count = 6;
        for (unsigned i = 0; i < 6; i++)
            out->operations[i] = (zmk_sensors_Operation)(i + 1);
        out->sensors.funcs.encode = encode_sensor;
        out->behavior_support.funcs.encode = encode_behaviors;
        out->has_limits = true;
        out->limits.max_sensors = SS_MAX_SENSORS;
        out->limits.max_name_bytes = 63;
        out->limits.max_response_bytes = 65536;
        out->has_state = true;
        token_to_wire(&out->state);
        out->save_guarantee = zmk_sensors_SaveGuarantee_STUDIO_BEST_EFFORT;
        out->storage_incompatible = description.incompatible;
        return;
    }
    case zmk_sensors_Request_read_layer_tag: {
        const zmk_sensors_ReadLayerRequest *in = &request->request.read_layer;
        if (!in->has_layer_id || !in->has_expected_state) {
            error(response, SS_INVALID_REQUEST);
            return;
        }
        struct ss_token expected = token_from_wire(&in->expected_state);
        int ret = ss_read_layer(port->engine, &expected, in->layer_id, reply_bindings,
                                &description.token);
        if (ret != SS_OK) {
            error(response, ret);
            return;
        }
        response->which_result = zmk_sensors_Response_layer_tag;
        zmk_sensors_ReadLayerResult *out = &response->result.layer;
        out->has_layer_id = true;
        out->layer_id = in->layer_id;
        out->bindings.funcs.encode = encode_bindings;
        out->has_state = true;
        token_to_wire(&out->state);
        out->has_unsaved_changes = unsaved();
        return;
    }
    case zmk_sensors_Request_set_binding_tag: {
        const zmk_sensors_SetBindingRequest *in = &request->request.set_binding;
        if (!in->has_layer_id || !in->has_binding || !in->binding.has_sensor_id ||
            !in->has_expected_state) {
            error(response, SS_INVALID_REQUEST);
            return;
        }
        struct ss_binding binding = {0};
        if (in->binding.which_value == zmk_sensors_SensorBinding_behavior_tag) {
            binding.behavior_id = in->binding.value.behavior.behavior_id;
            binding.param1 = in->binding.value.behavior.param1;
            binding.param2 = in->binding.value.behavior.param2;
        } else if (in->binding.which_value == zmk_sensors_SensorBinding_inherit_tag)
            binding.inherit = true;
        else {
            error(response, SS_INVALID_REQUEST);
            return;
        }
        struct ss_token expected = token_from_wire(&in->expected_state);
        int ret = ss_set_with_token(port->engine, &expected, in->layer_id, in->binding.sensor_id,
                                    &binding, &description.token);
        if (ret != SS_OK) {
            error(response, ret);
            return;
        }
        response->which_result = zmk_sensors_Response_binding_tag;
        zmk_sensors_SetBindingResult *out = &response->result.binding;
        out->has_layer_id = true;
        out->layer_id = in->layer_id;
        out->has_binding = true;
        binding_to_wire(in->binding.sensor_id, &binding, &out->binding);
        out->has_state = true;
        token_to_wire(&out->state);
        out->has_unsaved_changes = unsaved();
        return;
    }
    default:
        error(response, SS_INVALID_REQUEST);
        return;
    }
}
