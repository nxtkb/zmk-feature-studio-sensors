/* SPDX-License-Identifier: MIT */
#include <string.h>
#include <studio_sensors/engine.h>

static void lock(const struct ss_engine *e) {
    if (e->lock)
        e->lock(e->lock_context);
}
static void unlock(const struct ss_engine *e) {
    if (e->unlock)
        e->unlock(e->lock_context);
}
void ss_set_lock(struct ss_engine *e, void (*l)(void *), void (*u)(void *), void *context) {
    e->lock = l;
    e->unlock = u;
    e->lock_context = context;
}
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t crc = ~0u;
    while (n--) {
        crc ^= *p++;
        for (unsigned b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void write32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
static bool binding_equal(const struct ss_binding *a, const struct ss_binding *b) {
    return a->inherit == b->inherit &&
           (a->inherit ||
            (a->behavior_id == b->behavior_id && a->param1 == b->param1 && a->param2 == b->param2));
}
int ss_layer_index(const struct ss_engine *e, uint32_t id) {
    for (unsigned i = 0; i < e->layer_count; i++)
        if (e->layers[i] == id)
            return (int)i;
    return -1;
}
int ss_sensor_index(const struct ss_engine *e, uint32_t id) {
    for (unsigned i = 0; i < e->sensor_count; i++)
        if (e->sensors[i].id == id)
            return (int)i;
    return -1;
}
int ss_init(struct ss_engine *e, const struct ss_sensor *sensors, unsigned sensor_count,
            const uint32_t *layers, unsigned layer_count, const struct ss_binding *stock,
            const uint8_t generation[16], const uint8_t topology[16], ss_validate_fn validate) {
    if (!e || sensor_count > SS_MAX_SENSORS || !layer_count || layer_count > SS_MAX_LAYERS ||
        !generation || !topology || !validate || !layers || (sensor_count && (!sensors || !stock)))
        return SS_INVALID_REQUEST;
    for (unsigned i = 0; i < sensor_count; i++) {
        if (!sensors[i].key || !sensors[i].name)
            return SS_INVALID_REQUEST;
        for (unsigned j = 0; j < i; j++)
            if (sensors[i].id == sensors[j].id ||
                (sensors[i].key[0] && !strcmp(sensors[i].key, sensors[j].key)))
                return SS_INVALID_REQUEST;
    }
    for (unsigned i = 0; i < layer_count; i++)
        for (unsigned j = 0; j < i; j++)
            if (layers[i] == layers[j])
                return SS_INVALID_REQUEST;
    memset(e, 0, sizeof(*e));
    e->sensor_count = sensor_count;
    e->layer_count = layer_count;
    e->validate = validate;
    memcpy(e->token.generation, generation, 16);
    memcpy(e->token.topology, topology, 16);
    memcpy(e->storage_signature, topology, 16);
    if (sensor_count)
        memcpy(e->sensors, sensors, sensor_count * sizeof(*sensors));
    memcpy(e->layers, layers, layer_count * sizeof(*layers));
    for (unsigned l = 0; l < layer_count; l++)
        for (unsigned s = 0; s < sensor_count; s++) {
            struct ss_binding b = stock[l * sensor_count + s];
            /* Stock bindings are firmware-owned; missing optional editor metadata
             * must not disable execution. */
            e->stock[l][s] = e->saved[l][s] = e->live[l][s] = b;
        }
    return SS_OK;
}
bool ss_token_equal(const struct ss_token *a, const struct ss_token *b) {
    return a && b && a->revision == b->revision && !memcmp(a->generation, b->generation, 16) &&
           !memcmp(a->topology, b->topology, 16);
}
static void changed(struct ss_engine *e) {
    if (++e->token.revision == 0) {
        /* Rotate generation on wrap; callers supply a fresh generation on boot. */
        for (unsigned i = 0; i < 16; i++)
            if (++e->token.generation[i] != 0)
                break;
    }
}
void ss_changed(struct ss_engine *e) {
    lock(e);
    changed(e);
    unlock(e);
}
static bool unsaved_unlocked(const struct ss_engine *e) {
    for (unsigned l = 0; l < e->layer_count; l++)
        for (unsigned s = 0; s < e->sensor_count; s++)
            if (e->dirty[l][s])
                return true;
    return false;
}
static bool incompatible_unlocked(const struct ss_engine *e) {
    for (unsigned l = 0; l < e->layer_count; l++)
        for (unsigned s = 0; s < e->sensor_count; s++)
            if (e->incompatible[l][s])
                return true;
    return false;
}
bool ss_unsaved(const struct ss_engine *e) {
    lock(e);
    bool result = unsaved_unlocked(e);
    unlock(e);
    return result;
}
bool ss_incompatible(const struct ss_engine *e) {
    lock(e);
    bool result = incompatible_unlocked(e);
    unlock(e);
    return result;
}
void ss_describe(const struct ss_engine *e, struct ss_description *out) {
    lock(e);
    out->token = e->token;
    out->sensor_count = e->sensor_count;
    memcpy(out->sensors, e->sensors, sizeof(e->sensors));
    out->unsaved = unsaved_unlocked(e);
    out->incompatible = incompatible_unlocked(e);
    unlock(e);
}
int ss_read_layer(const struct ss_engine *e, const struct ss_token *expected, uint32_t layer,
                  struct ss_binding out[SS_MAX_SENSORS], struct ss_token *token) {
    lock(e);
    int l = ss_layer_index(e, layer), ret = SS_OK;
    if (!ss_token_equal(&e->token, expected))
        ret = SS_STATE_CONFLICT;
    else if (l < 0)
        ret = SS_INVALID_LAYER;
    else {
        memcpy(out, e->live[l], sizeof(e->live[l]));
        *token = e->token;
    }
    unlock(e);
    return ret;
}

int ss_get(const struct ss_engine *e, uint32_t layer, uint32_t sensor, struct ss_binding *out) {
    int l = ss_layer_index(e, layer), s = ss_sensor_index(e, sensor);
    if (l < 0)
        return SS_INVALID_LAYER;
    if (s < 0)
        return SS_INVALID_SENSOR;
    if (!out)
        return SS_INVALID_REQUEST;
    lock(e);
    *out = e->live[l][s];
    unlock(e);
    return SS_OK;
}
static int set_unlocked(struct ss_engine *e, const struct ss_token *expected, uint32_t layer,
                        uint32_t sensor, const struct ss_binding *binding) {
    if (!ss_token_equal(&e->token, expected))
        return SS_STATE_CONFLICT;
    int l = ss_layer_index(e, layer), s = ss_sensor_index(e, sensor);
    if (l < 0)
        return SS_INVALID_LAYER;
    if (s < 0)
        return SS_INVALID_SENSOR;
    if (!e->sensors[s].writable)
        return SS_NOT_SUPPORTED;
    if (!binding)
        return SS_INVALID_REQUEST;

    if (binding_equal(binding, &e->live[l][s]))
        return SS_OK;
    e->live[l][s] = *binding;
    e->dirty[l][s] = !binding_equal(binding, &e->saved[l][s]) || e->incompatible[l][s];
    changed(e);
    return SS_OK;
}
int ss_set_with_token(struct ss_engine *e, const struct ss_token *expected, uint32_t layer,
                      uint32_t sensor, const struct ss_binding *binding, struct ss_token *result) {
    if (!binding)
        return SS_INVALID_REQUEST;
    int ret = binding->inherit ? SS_OK : e->validate(binding);
    if (ret != SS_OK)
        return ret;
    lock(e);
    ret = set_unlocked(e, expected, layer, sensor, binding);
    if (ret == SS_OK && result)
        *result = e->token;
    unlock(e);
    return ret;
}
int ss_set(struct ss_engine *e, const struct ss_token *expected, uint32_t layer, uint32_t sensor,
           const struct ss_binding *binding) {
    return ss_set_with_token(e, expected, layer, sensor, binding, NULL);
}

void ss_record_encode(const struct ss_engine *e, const struct ss_binding *b,
                      uint8_t out[SS_RECORD_SIZE]) {
    memset(out, 0, SS_RECORD_SIZE);
    write32(out, 1);
    memcpy(out + 4, e->storage_signature, 16);
    write32(out + 20, b->inherit ? 1 : 0);
    write32(out + 24, (uint32_t)b->behavior_id);
    write32(out + 28, b->param1);
    write32(out + 32, b->param2);
    write32(out + 36, crc32(out, 36));
}
int ss_record_load(struct ss_engine *e, unsigned l, unsigned s, const uint8_t *record,
                   size_t length) {
    if (l >= e->layer_count || s >= e->sensor_count)
        return SS_INVALID_REQUEST;
    bool valid = record && length == SS_RECORD_SIZE && read32(record) == 1 &&
                 !memcmp(record + 4, e->storage_signature, 16) && read32(record + 20) <= 1 &&
                 read32(record + 36) == crc32(record, 36);
    struct ss_binding b = {0};
    if (valid) {
        b = (struct ss_binding){.inherit = read32(record + 20) == 1,
                                .behavior_id = (int32_t)read32(record + 24),
                                .param1 = read32(record + 28),
                                .param2 = read32(record + 32)};
        valid = b.inherit || e->validate(&b) == SS_OK;
    }
    if (!valid) {
        e->incompatible[l][s] = true;
        e->dirty[l][s] = true;
        return SS_INVALID_PARAMETERS;
    }
    e->live[l][s] = e->saved[l][s] = b;
    return SS_OK;
}
int ss_save(struct ss_engine *e, ss_write_fn write, void *context) {
    if (!write)
        return SS_NOT_SUPPORTED;
    for (unsigned l = 0; l < e->layer_count; l++)
        for (unsigned s = 0; s < e->sensor_count; s++) {
            lock(e);
            bool dirty = e->dirty[l][s];
            struct ss_binding snapshot = e->live[l][s];
            unlock(e);
            if (!dirty)
                continue;
            uint8_t record[SS_RECORD_SIZE];
            ss_record_encode(e, &snapshot, record);
            int ret = write(l, s, record, false, context);
            if (ret)
                return ret;
            lock(e);
            e->saved[l][s] = snapshot;
            e->incompatible[l][s] = false;
            e->dirty[l][s] = !binding_equal(&e->live[l][s], &snapshot);
            unlock(e);
        }
    ss_changed(e);
    return ss_unsaved(e) ? SS_STATE_CONFLICT : SS_OK;
}
int ss_discard(struct ss_engine *e) {
    lock(e);
    if (incompatible_unlocked(e)) {
        unlock(e);
        return SS_PERSISTENCE_FAILED;
    }
    memcpy(e->live, e->saved, sizeof(e->live));
    memset(e->dirty, 0, sizeof(e->dirty));
    for (unsigned l = 0; l < e->layer_count; l++)
        for (unsigned s = 0; s < e->sensor_count; s++)
            if (e->incompatible[l][s])
                e->dirty[l][s] = true;
    changed(e);
    unlock(e);
    return SS_OK;
}
int ss_reset(struct ss_engine *e, ss_write_fn write, void *context) {
    if (!write)
        return SS_NOT_SUPPORTED;
    for (unsigned l = 0; l < e->layer_count; l++)
        for (unsigned s = 0; s < e->sensor_count; s++) {
            int ret = write(l, s, NULL, true, context);
            if (ret)
                return ret;
            lock(e);
            e->live[l][s] = e->saved[l][s] = e->stock[l][s];
            e->dirty[l][s] = false;
            e->incompatible[l][s] = false;
            unlock(e);
        }
    ss_changed(e);
    return SS_OK;
}
