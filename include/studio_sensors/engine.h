/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef SS_MAX_SENSORS
#define SS_MAX_SENSORS 8
#endif
#define SS_MAX_LAYERS 32
#define SS_RECORD_SIZE 40

enum ss_error {
    SS_OK = 0,
    SS_INVALID_REQUEST = 1,
    SS_INVALID_SENSOR = 2,
    SS_INVALID_LAYER = 3,
    SS_INVALID_BEHAVIOR = 4,
    SS_INVALID_PARAMETERS = 5,
    SS_NOT_SUPPORTED = 6,
    SS_STATE_CONFLICT = 7,
    SS_LOCK_REQUIRED = 8,
    SS_PERSISTENCE_FAILED = 9,
    SS_NO_SPACE = 10,
};
struct ss_binding {
    int32_t behavior_id;
    uint32_t param1, param2;
    bool inherit;
};
struct ss_token {
    uint8_t generation[16], topology[16];
    uint32_t revision;
};
struct ss_sensor {
    uint32_t id;
    const char *key, *name;
    uint32_t kind, press_position;
    bool has_press, writable;
};
typedef int (*ss_validate_fn)(const struct ss_binding *binding);
typedef int (*ss_write_fn)(unsigned layer_index, unsigned sensor_index,
                           const uint8_t record[SS_RECORD_SIZE], bool remove, void *context);
struct ss_engine {
    struct ss_token token;
    uint8_t storage_signature[16];
    struct ss_sensor sensors[SS_MAX_SENSORS];
    uint32_t layers[SS_MAX_LAYERS];
    unsigned sensor_count, layer_count;
    struct ss_binding live[SS_MAX_LAYERS][SS_MAX_SENSORS];
    struct ss_binding saved[SS_MAX_LAYERS][SS_MAX_SENSORS];
    struct ss_binding stock[SS_MAX_LAYERS][SS_MAX_SENSORS];
    bool dirty[SS_MAX_LAYERS][SS_MAX_SENSORS];
    bool incompatible[SS_MAX_LAYERS][SS_MAX_SENSORS];
    ss_validate_fn validate;
    void (*lock)(void *context);
    void (*unlock)(void *context);
    void *lock_context;
};
int ss_init(struct ss_engine *e, const struct ss_sensor *sensors, unsigned sensor_count,
            const uint32_t *layers, unsigned layer_count, const struct ss_binding *stock,
            const uint8_t generation[16], const uint8_t topology[16], ss_validate_fn validate);
int ss_layer_index(const struct ss_engine *e, uint32_t id);
int ss_sensor_index(const struct ss_engine *e, uint32_t id);
bool ss_token_equal(const struct ss_token *a, const struct ss_token *b);
void ss_changed(struct ss_engine *e);
bool ss_unsaved(const struct ss_engine *e);
bool ss_incompatible(const struct ss_engine *e);
int ss_get(const struct ss_engine *e, uint32_t layer, uint32_t sensor, struct ss_binding *out);
int ss_set(struct ss_engine *e, const struct ss_token *expected, uint32_t layer, uint32_t sensor,
           const struct ss_binding *binding);
int ss_save(struct ss_engine *e, ss_write_fn write, void *context);
int ss_discard(struct ss_engine *e);
int ss_reset(struct ss_engine *e, ss_write_fn write, void *context);
void ss_record_encode(const struct ss_engine *e, const struct ss_binding *b,
                      uint8_t out[SS_RECORD_SIZE]);
int ss_record_load(struct ss_engine *e, unsigned layer, unsigned sensor, const uint8_t *record,
                   size_t length);

void ss_set_lock(struct ss_engine *e, void (*lock)(void *), void (*unlock)(void *), void *context);

struct ss_description {
    struct ss_token token;
    struct ss_sensor sensors[SS_MAX_SENSORS];
    unsigned sensor_count;
    bool unsaved, incompatible;
};
void ss_describe(const struct ss_engine *e, struct ss_description *out);
int ss_read_layer(const struct ss_engine *e, const struct ss_token *expected, uint32_t layer,
                  struct ss_binding out[SS_MAX_SENSORS], struct ss_token *token);
int ss_set_with_token(struct ss_engine *e, const struct ss_token *expected, uint32_t layer,
                      uint32_t sensor, const struct ss_binding *binding, struct ss_token *result);
