/* SPDX-License-Identifier: MIT */
#include "ss_build_config.h"
#include <drivers/behavior.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <studio_sensors/engine.h>
#include <studio_sensors/rpc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/physical_layouts.h>
#include <zmk/sensors.h>
#include <zmk/studio/rpc.h>

LOG_MODULE_REGISTER(studio_sensors, LOG_LEVEL_INF);
BUILD_ASSERT(ZMK_KEYMAP_SENSORS_LEN <= SS_MAX_SENSORS);
BUILD_ASSERT(ZMK_KEYMAP_LAYERS_LEN <= SS_MAX_LAYERS);
static struct ss_engine engine;
static bool ready;
static bool metadata_valid;
static struct k_spinlock state_lock;
static k_spinlock_key_t state_lock_key;
static uint8_t records[SS_MAX_LAYERS][SS_MAX_SENSORS][SS_RECORD_SIZE];
static bool have_record[SS_MAX_LAYERS][SS_MAX_SENSORS];
static char fallback_keys[SS_MAX_SENSORS][24];
static char fallback_names[SS_MAX_SENSORS][32];
static struct ss_sensor descriptors[SS_MAX_SENSORS];
static struct ss_binding stock[SS_MAX_LAYERS * SS_MAX_SENSORS];
static uint32_t layer_ids[SS_MAX_LAYERS];
static void take_lock(void *ctx) { state_lock_key = k_spin_lock(&state_lock); }
static void release_lock(void *ctx) { k_spin_unlock(&state_lock, state_lock_key); }
#define SS_META DT_INST(0, nxtkb_studio_sensors)
#if DT_NODE_EXISTS(SS_META)
BUILD_ASSERT(DT_PROP_LEN(SS_META, sensor_ids) == ZMK_KEYMAP_SENSORS_LEN);
BUILD_ASSERT(DT_PROP_LEN(SS_META, sensor_keys) == ZMK_KEYMAP_SENSORS_LEN);
BUILD_ASSERT(DT_PROP_LEN(SS_META, sensor_names) == ZMK_KEYMAP_SENSORS_LEN);
BUILD_ASSERT(DT_PROP_LEN(SS_META, sensor_kinds) == ZMK_KEYMAP_SENSORS_LEN);
BUILD_ASSERT(DT_PROP_LEN(SS_META, press_positions) == ZMK_KEYMAP_SENSORS_LEN);
static const uint32_t declared_ids[] = DT_PROP(SS_META, sensor_ids);
static const char *const declared_keys[] = DT_PROP(SS_META, sensor_keys);
static const char *const declared_names[] = DT_PROP(SS_META, sensor_names);
static const uint32_t declared_kinds[] = DT_PROP(SS_META, sensor_kinds);
static const uint32_t declared_press[] = DT_PROP(SS_META, press_positions);
#endif
#define SS_DEFAULT_KIND(i, _)                                                                      \
    COND_CODE_1(DT_NODE_HAS_COMPAT(ZMK_KEYMAP_SENSORS_BY_IDX(i), alps_ec11), (1), (0))
#if ZMK_KEYMAP_HAS_SENSORS && ZMK_KEYMAP_SENSORS_LEN > 0
static const uint32_t default_kinds[] = {LISTIFY(ZMK_KEYMAP_SENSORS_LEN, SS_DEFAULT_KIND, (, ), _)};
#endif
static const char *storage_key(unsigned sensor) {
    return descriptors[sensor].key[0] ? descriptors[sensor].key : fallback_keys[sensor];
}
static int metadata_init(void) {
    for (unsigned i = 0; i < ZMK_KEYMAP_SENSORS_LEN; i++) {
        snprintf(fallback_keys[i], sizeof(fallback_keys[i]), "index-%u", i);
        snprintf(fallback_names[i], sizeof(fallback_names[i]), "Encoder %u", i + 1);
#if DT_NODE_EXISTS(SS_META)
        descriptors[i] = (struct ss_sensor){.id = declared_ids[i],
                                            .key = declared_keys[i],
                                            .name = declared_names[i],
                                            .kind = declared_kinds[i],
                                            .has_press = declared_press[i] != UINT32_MAX,
                                            .press_position = declared_press[i],
                                            .writable = declared_kinds[i] == 1};
#else
        descriptors[i] = (struct ss_sensor){
            .id = i,
            .key = "",
            .name = fallback_names[i],
#if ZMK_KEYMAP_HAS_SENSORS && ZMK_KEYMAP_SENSORS_LEN > 0
            .kind = default_kinds[i],
            .writable = default_kinds[i] == 1,
#endif
        };
#endif
        if (strlen(descriptors[i].key) > 39 || strlen(descriptors[i].name) > 63)
            return -EINVAL;
        for (const char *p = storage_key(i); *p; p++)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.'))
                return -EINVAL;
    }
    metadata_valid = true;
    return 0;
}
SYS_INIT(metadata_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
static int validate(const struct ss_binding *in) {
    if (in->behavior_id <= 0 || in->behavior_id > UINT16_MAX)
        return SS_INVALID_BEHAVIOR;
    const char *name = zmk_behavior_find_behavior_name_from_local_id(in->behavior_id);
    const struct device *dev = name ? zmk_behavior_get_binding(name) : NULL;
    const struct behavior_driver_api *api = dev ? dev->api : NULL;
    if (!api || !api->sensor_binding_accept_data || !api->sensor_binding_process)
        return SS_INVALID_BEHAVIOR;
    struct zmk_behavior_binding binding = {
        .behavior_dev = name, .param1 = in->param1, .param2 = in->param2};
    return zmk_behavior_validate_binding(&binding) < 0 ? SS_INVALID_PARAMETERS : SS_OK;
}
static unsigned behavior_count(void) {
    unsigned count = 0;
    STRUCT_SECTION_FOREACH(zmk_behavior_local_id_map, item) { count++; }
    return count;
}
static bool behavior_at(unsigned index, struct ss_behavior *out) {
    unsigned i = 0;
    STRUCT_SECTION_FOREACH(zmk_behavior_local_id_map, item) {
        if (i++ != index)
            continue;
        const struct behavior_driver_api *api = item->device->api;
        *out = (struct ss_behavior){.id = item->local_id,
                                    .key = api->binding_pressed || api->binding_released,
                                    .sensor = api->sensor_binding_accept_data &&
                                              api->sensor_binding_process,
                                    .param1_role = api->sensor_param1_role,
                                    .param2_role = api->sensor_param2_role};
        return device_is_ready(item->device);
    }
    return false;
}
static void refresh_layout(void) {
    uint32_t const *map = NULL;
    int count = zmk_physical_layouts_get_selected_to_stock_position_map(&map);
    take_lock(NULL);
    for (unsigned i = 0; i < engine.sensor_count; i++) {
        engine.sensors[i].has_press = false;
        if (!descriptors[i].has_press || count < 0)
            continue;
        for (int p = 0; p < count; p++)
            if (map[p] == descriptors[i].press_position) {
                engine.sensors[i].has_press = true;
                engine.sensors[i].press_position = p;
                break;
            }
    }
    memcpy(engine.token.topology, ss_build_signature, 16);
    uint32_t selected = zmk_physical_layouts_get_selected();
    for (unsigned b = 0; b < 4; b++)
        engine.token.topology[12 + b] ^= (uint8_t)(selected >> (8 * b));
    release_lock(NULL);
}
static int ss_settings_set(const char *name, size_t length, settings_read_cb read, void *arg) {
    if (!metadata_valid)
        return -EINVAL;
    char *end;
    unsigned long layer = strtoul(name, &end, 10);
    if (end == name || *end != '/' || layer >= ZMK_KEYMAP_LAYERS_LEN)
        return -EINVAL;
    for (unsigned i = 0; i < ZMK_KEYMAP_SENSORS_LEN; i++) {
        if (strcmp(end + 1, storage_key(i)))
            continue;
        if (length != SS_RECORD_SIZE ||
            read(arg, records[layer][i], SS_RECORD_SIZE) != SS_RECORD_SIZE) {
            memset(records[layer][i], 0, SS_RECORD_SIZE);
        }
        have_record[layer][i] = true;
        return 0;
    }
    return 0; /* Retain records for hardware no longer present; never reinterpret
                 an index. */
}
static int ss_settings_commit(void) {
    if (!metadata_valid)
        return -EINVAL;
    uint8_t generation[16];
    sys_rand_get(generation, sizeof(generation));
    for (unsigned l = 0; l < ZMK_KEYMAP_LAYERS_LEN; l++) {
        layer_ids[l] = l;
        for (unsigned i = 0; i < ZMK_KEYMAP_SENSORS_LEN; i++) {
            struct zmk_behavior_binding binding;
            zmk_keymap_get_sensor_stock_binding(l, i, &binding);
            stock[l * ZMK_KEYMAP_SENSORS_LEN + i] = (struct ss_binding){
                .inherit = !binding.behavior_dev,
                .behavior_id =
                    binding.behavior_dev ? zmk_behavior_get_local_id(binding.behavior_dev) : 0,
                .param1 = binding.param1,
                .param2 = binding.param2};
        }
    }
    int ret = ss_init(&engine, descriptors, ZMK_KEYMAP_SENSORS_LEN, layer_ids,
                      ZMK_KEYMAP_LAYERS_LEN, stock, generation, ss_build_signature, validate);
    if (ret) {
        LOG_ERR("Sensor initialization failed: %d", ret);
        return -EINVAL;
    }
    ss_set_lock(&engine, take_lock, release_lock, NULL);
    for (unsigned l = 0; l < engine.layer_count; l++)
        for (unsigned i = 0; i < engine.sensor_count; i++)
            if (have_record[l][i])
                ss_record_load(&engine, l, i, records[l][i], SS_RECORD_SIZE);
    refresh_layout();
    ready = true;
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(studio_sensors_local, "ss01", NULL, ss_settings_set,
                                          ss_settings_commit, NULL, 100);
static int write_record(unsigned l, unsigned s, const uint8_t *record, bool remove, void *context) {
    char name[64];
    snprintf(name, sizeof(name), "ss01/%u/%s", l, storage_key(s));
    int ret = remove ? settings_delete(name) : settings_save_one(name, record, SS_RECORD_SIZE);
    if (ret < 0)
        return ret == -ENOSPC ? SS_NO_SPACE : SS_PERSISTENCE_FAILED;
    have_record[l][s] = !remove;
    if (!remove)
        memcpy(records[l][s], record, SS_RECORD_SIZE);
    return SS_OK;
}
int zmk_keymap_get_sensor_binding_copy(uint8_t layer, uint16_t sensor,
                                       struct zmk_behavior_binding *out) {
    if (!ready)
        return zmk_keymap_get_sensor_stock_binding(layer, sensor, out);
    if (sensor >= engine.sensor_count)
        return -EINVAL;
    struct ss_binding binding;
    if (ss_get(&engine, layer, engine.sensors[sensor].id, &binding))
        return -EINVAL;
    *out = (struct zmk_behavior_binding){
        .behavior_dev = binding.inherit
                            ? NULL
                            : zmk_behavior_find_behavior_name_from_local_id(binding.behavior_id),
        .param1 = binding.param1,
        .param2 = binding.param2};
    return 0;
}
int zmk_keymap_sensors_check_unsaved_changes(void) { return ready && ss_unsaved(&engine); }
int zmk_keymap_sensors_save_changes(void) {
    int ret = ready ? ss_save(&engine, write_record, NULL) : SS_OK;
    return ret == SS_OK ? 0 : ret == SS_NO_SPACE ? -ENOSPC : -EIO;
}
int zmk_keymap_sensors_discard_changes(void) {
    return ready && ss_discard(&engine) != SS_OK ? -EIO : 0;
}
int zmk_keymap_sensors_reset_settings(void) {
    return ready && ss_reset(&engine, write_record, NULL) ? -EIO : 0;
}
void zmk_keymap_sensors_changed(void) {
    if (ready) {
        refresh_layout();
        ss_changed(&engine);
    }
}
static bool other_unsaved(void) {
    return zmk_keymap_check_unsaved_changes() > 0 ||
           zmk_physical_layouts_check_unsaved_selection() > 0;
}
static const struct ss_rpc_port rpc_port = {.engine = &engine,
                                            .behavior_count = behavior_count,
                                            .behavior_at = behavior_at,
                                            .other_unsaved = other_unsaved};
static zmk_studio_Response sensors(const zmk_studio_Request *request) {
    if (!ready)
        return ZMK_RPC_SIMPLE_ERR(GENERIC);
    ss_rpc_init(&rpc_port);
    zmk_sensors_Response response;
    ss_rpc_handle(&request->subsystem.keymap.request_type.sensors, &response);
    if (request->subsystem.keymap.request_type.sensors.which_request ==
            zmk_sensors_Request_set_binding_tag &&
        response.which_result == zmk_sensors_Response_binding_tag) {
        raise_zmk_studio_rpc_notification((struct zmk_studio_rpc_notification){
            .notification =
                ZMK_RPC_NOTIFICATION(keymap, unsaved_changes_status_changed, other_unsaved())});
    }
    return ZMK_RPC_RESPONSE(keymap, sensors, response);
}
ZMK_RPC_SUBSYSTEM_HANDLER(keymap, sensors, ZMK_STUDIO_RPC_HANDLER_SECURED);
