/* SPDX-License-Identifier: MIT */
#pragma once
#include <proto/zmk/sensors.pb.h>
#include <studio_sensors/engine.h>

#define SS_PROTOCOL_ID "com.nxtkb.studio.sensors.local/0.1"
struct ss_behavior {
    int32_t id;
    bool key, sensor;
    uint8_t param1_role, param2_role;
};
struct ss_rpc_port {
    struct ss_engine *engine;
    unsigned (*behavior_count)(void);
    bool (*behavior_at)(unsigned index, struct ss_behavior *out);
    bool (*other_unsaved)(void);
};
void ss_rpc_init(const struct ss_rpc_port *port);
void ss_rpc_handle(const zmk_sensors_Request *request, zmk_sensors_Response *response);
