/* SPDX-License-Identifier: MIT */
/* Host-only port around the actual pinned/patched Studio transmit functions. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define FRAMING_SOF 0xab
#define FRAMING_ESC 0xac
#define FRAMING_EOF 0xad
#define CONFIG_NANOPB_NO_ERRMSG 1
#define IS_ENABLED(x) (x)
#define K_FOREVER 0
#define LOG_WRN(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
struct ring_buf {
    uint8_t data[128];
    unsigned capacity, head, tail, used;
};
static struct ring_buf rpc_tx_buf;
static struct ring_buf rpc_rx_buf;
static uint8_t rx_output[1024];
static unsigned rx_output_len, rx_stalls;
static bool rx_notified;
static uint8_t output[32768];
static unsigned output_len, async_budget;
static int rpc_transport_mutex;
static void drain(unsigned count) {
    while (count-- && rpc_tx_buf.used) {
        assert(output_len < sizeof(output));
        output[output_len++] = rpc_tx_buf.data[rpc_tx_buf.head++];
        rpc_tx_buf.head %= rpc_tx_buf.capacity;
        rpc_tx_buf.used--;
    }
}
static void k_msleep(int delay) {
    (void)delay;
    if (rx_notified) {
        while (rpc_rx_buf.used) {
            rx_output[rx_output_len++] = rpc_rx_buf.data[rpc_rx_buf.head++];
            rpc_rx_buf.head %= rpc_rx_buf.capacity;
            rpc_rx_buf.used--;
        }
        rx_notified = false;
        rx_stalls = 0;
    }
    drain(27);
}
static void k_mutex_lock(int *lock, int timeout) {
    (void)timeout;
    assert(!*lock);
    *lock = 1;
}
static void k_mutex_unlock(int *lock) {
    assert(*lock);
    *lock = 0;
}
static uint32_t ring_buf_put(struct ring_buf *b, const uint8_t *p, uint32_t n) {
    unsigned added = 0;
    while (added < n && b->used < b->capacity) {
        b->data[b->tail++] = p[added++];
        b->tail %= b->capacity;
        b->used++;
    }
    return added;
}
static uint32_t ring_buf_put_claim(struct ring_buf *b, uint8_t **p, uint32_t n) {
    *p = &b->data[b->tail];
    if (b == &rpc_rx_buf && b->used == b->capacity && ++rx_stalls > 1000) {
        fputs("RX producer spins without waking/yielding to decoder\n", stderr);
        exit(3);
    }
    return MIN(n, MIN(b->capacity - b->used, b->capacity - b->tail));
}
static void ring_buf_put_finish(struct ring_buf *b, unsigned n) {
    assert(n <= b->capacity - b->used);
    b->tail = (b->tail + n) % b->capacity;
    b->used += n;
}
typedef struct pb_ostream_s pb_ostream_t;
struct pb_ostream_s {
    bool (*callback)(pb_ostream_t *, const uint8_t *, size_t);
    void *state;
    size_t max_size, bytes_written;
};
typedef struct {
    const uint8_t *payload;
    size_t length;
} zmk_studio_Response;
static const int zmk_studio_Response_msg = 0;
static bool pb_encode(pb_ostream_t *stream, const int *fields, const zmk_studio_Response *r) {
    (void)fields;
    return !r->length || stream->callback(stream, r->payload, r->length);
}
static void mock_notify(struct ring_buf *buffer, size_t added, bool done, void *data) {
    (void)added;
    (void)data;
    if (done)
        drain(buffer->used);
    else if (async_budget && buffer->used == buffer->capacity) {
        drain(27);
        async_budget--;
    }
}
struct mock_transport {
    void *(*tx_user_data)(void);
    void (*tx_notify)(struct ring_buf *, size_t, bool, void *);
};
static struct mock_transport mock = {.tx_notify = mock_notify};
static struct mock_transport *selected_transport = &mock;

#include "native_tx.inc"

typedef int atomic_t;
struct bt_conn {
    int unused;
};
struct bt_gatt_attr {
    int unused;
};
static bool handling_rx = true;
static struct ring_buf *zmk_rpc_get_rx_buf(void) { return &rpc_rx_buf; }
static void zmk_rpc_rx_notify(void) { rx_notified = true; }
static struct bt_conn connection;
static atomic_t notify_size;
static unsigned submissions;
static int notify_tx_work;
struct gatt_write_state {
    size_t pending_notify;
};
static struct bt_conn *zmk_ble_active_profile_conn(void) { return &connection; }
static uint16_t get_notify_size_for_conn(struct bt_conn *conn) {
    (void)conn;
    return 71;
}
static void bt_conn_unref(struct bt_conn *conn) { (void)conn; }
static void atomic_set(atomic_t *p, atomic_t n) { *p = n; }
static atomic_t atomic_get(atomic_t *p) { return *p; }
static void k_work_submit(int *work) {
    (void)work;
    submissions++;
}
#include "native_gatt.inc"

static void exercise_rx(unsigned prefilled, unsigned length) {
    memset(&rpc_rx_buf, 0, sizeof(rpc_rx_buf));
    rpc_rx_buf.capacity = 30;
    rx_output_len = rx_stalls = 0;
    rx_notified = false;
    uint8_t expected[256], payload[128];
    for (unsigned i = 0; i < prefilled; i++) {
        expected[i] = 0x55;
        assert(ring_buf_put(&rpc_rx_buf, expected + i, 1) == 1);
    }
    for (unsigned i = 0; i < length; i++)
        expected[prefilled + i] = payload[i] = (uint8_t)i;
    assert(write_rpc_req(&connection, NULL, payload, length, 0, 0) == length);
    k_msleep(1);
    assert(rx_output_len == prefilled + length);
    assert(!memcmp(rx_output, expected, rx_output_len));
}

static void exercise(const uint8_t *payload, size_t n, unsigned capacity, bool prefilled) {
    memset(&rpc_tx_buf, 0, sizeof(rpc_tx_buf));
    rpc_tx_buf.capacity = capacity;
    output_len = 0;
    async_budget = 1;
    if (prefilled)
        for (unsigned i = 0; i < capacity; i++) {
            uint8_t byte = 0x55;
            assert(ring_buf_put(&rpc_tx_buf, &byte, 1) == 1);
        }
    zmk_studio_Response r = {payload, n};
    assert(send_response(&r) == 0);
    drain(rpc_tx_buf.used);
    uint8_t expected[32768];
    unsigned length = 0;
    if (prefilled)
        for (unsigned i = 0; i < capacity; i++)
            expected[length++] = 0x55;
    expected[length++] = FRAMING_SOF;
    for (unsigned i = 0; i < n; i++) {
        if (payload[i] >= FRAMING_SOF && payload[i] <= FRAMING_EOF)
            expected[length++] = FRAMING_ESC;
        expected[length++] = payload[i];
    }
    expected[length++] = FRAMING_EOF;
    if (output_len != length || memcmp(output, expected, length)) {
        fprintf(stderr,
                "Frame mismatch: payload=%zu capacity=%u prefilled=%d bytes=%u expected=%u\n", n,
                capacity, prefilled, output_len, length);
        exit(2);
    }
}
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--rx-regression-only")) {
        exercise_rx(20, 20);
        return 0;
    }
    uint8_t payload[1024];
    memset(payload, 0x42, sizeof(payload));
    /* Deterministically leave a 64-byte ring full just before the EOF write. */
    exercise(payload, 90, 64, false);
    if (argc > 1)
        return 0;
    for (unsigned n = 0; n <= sizeof(payload); n++)
        exercise(payload, n, 64, false);
    for (unsigned i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)i;
    unsigned sizes[] = {16, 27, 64, 128};
    for (unsigned c = 0; c < 4; c++)
        for (unsigned n = 0; n <= sizeof(payload); n++) {
            exercise(payload, n, sizes[c], false);
            exercise(payload, n, sizes[c], true);
        }
    refresh_notify_size();
    assert(notify_size > 0 && notify_size <= 27);
    struct gatt_write_state state = {0};
    submissions = 0;
    gatt_tx_notify(&rpc_tx_buf, 27, false, &state);
    assert(submissions == 1 && state.pending_notify == 0);
    for (unsigned prefilled = 0; prefilled <= 30; prefilled++)
        for (unsigned length = 0; length <= 128; length++)
            exercise_rx(prefilled, length);
    puts("PASS: exact-full EOF, full-ring SOF, escaped/chunked payloads and BLE flush threshold");
    puts("PASS: saturated RX wakes/yields to decoder; all bytes retained across 30-byte ring");
    return 0;
}
