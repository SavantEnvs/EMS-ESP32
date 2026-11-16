#pragma once

#include <cstddef>
#include <cstdint>

// One-time start-up of the native (EMSESP_STANDALONE) EMS-ESP application, mirroring main() in
// test/test_api/test_api.cpp: the web server stubs, the application itself, test data for the custom
// entities / sensors / scheduler, and one EMS device of every kind so the telegram handlers are registered.
void emsesp_env_init();

// Feed one EMS bus telegram (src dest type offset data..., WITHOUT the trailing CRC) through the same
// entry point the UART driver uses. The CRC is appended with the library's own calculate_crc(), as the
// upstream unit test does, then the Rx queue is drained. Before the telegram the EMS side of the
// application is put back to its start-up state (devices, Rx/Tx services, bus and room-control state,
// custom-entity values, system status), so the outcome depends on this telegram alone.
void emsesp_env_rx_telegram(const uint8_t * body, size_t length);

// Put the application back to its start-up state, as emsesp_env_rx_telegram() does before a telegram. For a
// harness that runs commands in the application (admin-level ones included) and must not carry their effects
// to the next input.
void emsesp_env_reset();

// Dispatch a REST API request path ("/api/<device>/<cmd>...") to Command::process() as a non-admin query,
// the way WebAPIService::parse() does for a request without a value. body == nullptr is a GET; otherwise it
// is the POST body, which must be a JSON object. Returns false, without calling the library, for a request
// that carries a value (a "data"/"value" key, or a body that is not a JSON object), because that is a write.
bool emsesp_env_api(const char * url, const char * body);
