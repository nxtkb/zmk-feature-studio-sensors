/* SPDX-License-Identifier: MIT — test-only host port of the production C
 * engine/RPC. */
#include <pb_decode.h>
#include <pb_encode.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <studio_sensors/rpc.h>

static struct ss_engine engine;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t saved[3][2][SS_RECORD_SIZE];
static bool have_saved[3][2];
static unsigned count = 3, boot_number;
static int fail_after = -1;
static bool other_dirty;
static bool mutate_on_save;
static uint8_t abi_version;
static const uint32_t ids[] = {0, 17, 29};
static const char *keys[] = {"main", "aux", "third"};
static const char *names[] = {"Main dial", "Aux dial", "Third dial"};
static void take(void *context) { pthread_mutex_lock(&mutex); }
static void release(void *context) { pthread_mutex_unlock(&mutex); }
static int validate(const struct ss_binding *b) {
    if (b->behavior_id == 42)
        return (b->param1 >> 16 && b->param2 >> 16) ? SS_OK : SS_INVALID_PARAMETERS;
    if (b->behavior_id == 43)
        return b->param1 <= 2 && b->param2 == 0 ? SS_OK : SS_INVALID_PARAMETERS;
    if (b->behavior_id == 44)
        return b->param1 <= 4 && b->param2 == 0 ? SS_OK : SS_INVALID_PARAMETERS;
    return SS_INVALID_BEHAVIOR;
}
static unsigned behavior_count(void) { return 4; }
static bool behavior_at(unsigned index, struct ss_behavior *out) {
    *out = index == 0 ? (struct ss_behavior){.id = 7, .key = true}
           : index == 1
               ? (struct ss_behavior){.id = 42, .sensor = true, .param1_role = 1, .param2_role = 2}
           : index == 2 ? (struct ss_behavior){.id = 43, .sensor = true}
                        : (struct ss_behavior){.id = 44, .key = true, .sensor = true};
    return index < 4;
}
static bool other_unsaved(void) { return other_dirty; }
static const struct ss_rpc_port port = {.engine = &engine,
                                        .behavior_count = behavior_count,
                                        .behavior_at = behavior_at,
                                        .other_unsaved = other_unsaved};
static int slot(uint32_t id) {
    for (unsigned i = 0; i < 3; i++)
        if (ids[i] == id)
            return i;
    return -1;
}
static int write_record(unsigned l, unsigned s, const uint8_t *record, bool remove, void *context) {
    if (fail_after == 0)
        return SS_NO_SPACE;
    if (fail_after > 0)
        fail_after--;
    int i = slot(engine.sensors[s].id);
    if (i < 0 || l >= 2)
        return SS_INVALID_REQUEST;
    have_saved[i][l] = !remove;
    if (!remove)
        memcpy(saved[i][l], record, SS_RECORD_SIZE);
    if (!remove && mutate_on_save) {
        mutate_on_save = false;
        struct ss_token expected = engine.token;
        struct ss_binding replacement = {.behavior_id = 42, .param1 = 0x70004, .param2 = 0x70005};
        ss_set(&engine, &expected, engine.layers[l], engine.sensors[s].id, &replacement);
    }
    return SS_OK;
}
int ss_host_boot(unsigned sensors, bool clear) {
    if (sensors > 3)
        return SS_INVALID_REQUEST;
    count = sensors;
    other_dirty = false;
    fail_after = -1;
    if (clear)
        memset(have_saved, 0, sizeof(have_saved));
    struct ss_sensor desc[3];
    struct ss_binding bindings[6];
    uint32_t layers[] = {0, 8};
    uint8_t generation[16] = {0}, topology[16] = {1, 2, 3, 4};
    generation[0] = ++boot_number;
    topology[15] = abi_version;
    for (unsigned s = 0; s < count; s++) {
        desc[s] = (struct ss_sensor){.id = ids[s],
                                     .key = keys[s],
                                     .name = names[s],
                                     .kind = 1,
                                     .has_press = s == 0,
                                     .press_position = 0,
                                     .writable = true};
        for (unsigned l = 0; l < 2; l++)
            bindings[l * count + s] =
                (struct ss_binding){.behavior_id = 42, .param1 = 0xC00EA, .param2 = 0xC00E9};
    }
    int ret = ss_init(&engine, desc, count, layers, 2, bindings, generation, topology, validate);
    if (ret)
        return ret;
    ss_set_lock(&engine, take, release, NULL);
    for (unsigned s = 0; s < count; s++)
        for (unsigned l = 0; l < 2; l++)
            if (have_saved[s][l])
                ss_record_load(&engine, l, s, saved[s][l], SS_RECORD_SIZE);
    ss_rpc_init(&port);
    return SS_OK;
}
int ss_host_call(const uint8_t *bytes, size_t length, uint8_t *out, size_t capacity) {
    zmk_sensors_Request request = zmk_sensors_Request_init_zero;
    pb_istream_t input = pb_istream_from_buffer(bytes, length);
    if (!pb_decode(&input, &zmk_sensors_Request_msg, &request))
        return -1;
    zmk_sensors_Response response;
    ss_rpc_handle(&request, &response);
    pb_ostream_t output = pb_ostream_from_buffer(out, capacity);
    if (!pb_encode(&output, &zmk_sensors_Response_msg, &response))
        return -2;
    return (int)output.bytes_written;
}
int ss_host_save(void) {
    int ret = ss_save(&engine, write_record, NULL);
    if (!ret)
        other_dirty = false;
    return ret;
}
int ss_host_discard(void) {
    int ret = ss_discard(&engine);
    if (!ret)
        other_dirty = false;
    return ret;
}
int ss_host_reset(void) {
    int ret = ss_reset(&engine, write_record, NULL);
    if (!ret)
        other_dirty = false;
    return ret;
}
void ss_host_ordinary_change(void) {
    other_dirty = true;
    ss_changed(&engine);
}
void ss_host_fail_after(int number) { fail_after = number; }
void ss_host_corrupt(void) {
    if (have_saved[0][0])
        saved[0][0][28] ^= 1;
}
void ss_host_reorder(void) {
    if (count < 2)
        return;
    struct ss_sensor descriptor = engine.sensors[0];
    engine.sensors[0] = engine.sensors[1];
    engine.sensors[1] = descriptor;
    for (unsigned l = 0; l < 2; l++) {
        struct ss_binding b = engine.live[l][0];
        engine.live[l][0] = engine.live[l][1];
        engine.live[l][1] = b;
        b = engine.saved[l][0];
        engine.saved[l][0] = engine.saved[l][1];
        engine.saved[l][1] = b;
        b = engine.stock[l][0];
        engine.stock[l][0] = engine.stock[l][1];
        engine.stock[l][1] = b;
        bool dirty = engine.dirty[l][0];
        engine.dirty[l][0] = engine.dirty[l][1];
        engine.dirty[l][1] = dirty;
    }
    engine.token.topology[15]++;
    ss_changed(&engine);
}

void ss_host_abi_change(void) { abi_version++; }
void ss_host_change_during_save(void) { mutate_on_save = true; }
static atomic_bool stress_failed;
static void *stress_reader(void *unused) {
    for (unsigned i = 0; i < 50000; i++) {
        struct ss_binding b;
        if (ss_get(&engine, 0, 0, &b) != SS_OK || b.behavior_id != 42 ||
            !((b.param1 == 0x70004 && b.param2 == 0x70005) ||
              (b.param1 == 0xC00EA && b.param2 == 0xC00E9)))
            atomic_store(&stress_failed, true);
    }
    return NULL;
}
int ss_host_stress(void) {
    ss_host_boot(3, true);
    atomic_store(&stress_failed, false);
    pthread_t reader;
    pthread_create(&reader, NULL, stress_reader, NULL);
    for (unsigned i = 0; i < 50000; i++) {
        struct ss_token expected = engine.token;
        struct ss_binding b = {.behavior_id = 42,
                               .param1 = (i & 1) ? 0x70004 : 0xC00EA,
                               .param2 = (i & 1) ? 0x70005 : 0xC00E9};
        if (ss_set(&engine, &expected, 0, 0, &b) != SS_OK)
            atomic_store(&stress_failed, true);
    }
    pthread_join(reader, NULL);
    return atomic_load(&stress_failed) ? 1 : 0;
}
