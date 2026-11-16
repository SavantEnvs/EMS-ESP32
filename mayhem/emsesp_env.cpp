// Shared start-up for the EMS-ESP harnesses. Follows main() / uart_telegram() / add_devices() in
// test/test_api/test_api.cpp so the fuzzed application is in the state the upstream unit tests use.

#include "emsesp_env.h"

#include <Arduino.h>
#include <emsesp.h>
#include <roomcontrol.h>

#include "ESPAsyncWebServer.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

using namespace emsesp;

// lib_standalone/Arduino.cpp (built with EMSESP_UNITY) still references the Arduino entry points that
// src/core/main.cpp provides; that file is left out of the fuzz build so there is a single EMSESP instance.
void setup() {
}
void loop() {
}

namespace {

// Access to the private static members that make up the telegram path's state (explicit instantiation
// ignores access control, so upstream's headers stay untouched).
template <typename Tag, typename Tag::type Ptr>
struct Rob {
    friend typename Tag::type rob(Tag) {
        return Ptr;
    }
};

#define STATE_MEMBER(CLASS, NAME, TYPE)                                \
    struct CLASS##_##NAME {                                            \
        using type = std::add_pointer_t<TYPE>;                         \
        friend type rob(CLASS##_##NAME);                               \
    };                                                                 \
    template struct Rob<CLASS##_##NAME, &emsesp::CLASS::NAME>;

STATE_MEMBER(EMSbus, last_bus_activity_, uint32_t)
STATE_MEMBER(EMSbus, bus_uptime_start_, uint32_t)
STATE_MEMBER(EMSbus, bus_connected_, bool)
STATE_MEMBER(EMSbus, ems_mask_, uint8_t)
STATE_MEMBER(EMSbus, ems_bus_id_, uint8_t)
STATE_MEMBER(EMSbus, tx_mode_, uint8_t)
STATE_MEMBER(EMSbus, tx_state_, uint8_t)
STATE_MEMBER(EMSbus, isEMS2_, bool)

STATE_MEMBER(EMSESP, watch_id_, uint16_t)
STATE_MEMBER(EMSESP, watch_, uint8_t)
STATE_MEMBER(EMSESP, read_id_, uint16_t)
STATE_MEMBER(EMSESP, read_next_, bool)
STATE_MEMBER(EMSESP, publish_id_, uint16_t)
STATE_MEMBER(EMSESP, response_id_, uint16_t)
STATE_MEMBER(EMSESP, tap_water_active_, bool)
STATE_MEMBER(EMSESP, publish_all_idx_, uint8_t)
STATE_MEMBER(EMSESP, unique_id_count_, uint8_t)
STATE_MEMBER(EMSESP, trace_raw_, bool)
STATE_MEMBER(EMSESP, wait_validate_, uint16_t)
STATE_MEMBER(EMSESP, wait_km_, bool)
STATE_MEMBER(EMSESP, last_fetch_, uint32_t)
STATE_MEMBER(EMSESP, last_entity_change_, uint32_t)
STATE_MEMBER(EMSESP, entity_compaction_pending_, bool)

STATE_MEMBER(Roomctrl, switch_off_, bool[4])
STATE_MEMBER(Roomctrl, send_time_, uint32_t[4])
STATE_MEMBER(Roomctrl, receive_time_, uint32_t[4])
STATE_MEMBER(Roomctrl, remotetemp_, int16_t[4])
STATE_MEMBER(Roomctrl, remotehum_, uint8_t[4])
STATE_MEMBER(Roomctrl, sendtype_, uint8_t[4])
STATE_MEMBER(Roomctrl, type_, uint8_t[4])
STATE_MEMBER(Roomctrl, timeout_, uint32_t)

// the values of those members right after start-up, put back before every telegram
struct SavedMember {
    void *        addr;
    size_t        size;
    unsigned char bytes[16];
};
std::vector<SavedMember> saved_members;

template <typename T>
void save_member(T * p) {
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= sizeof(SavedMember::bytes));
    SavedMember m{p, sizeof(T), {}};
    memcpy(m.bytes, p, sizeof(T));
    saved_members.push_back(m);
}

#define SAVE_MEMBER(CLASS, NAME) save_member(rob(CLASS##_##NAME()));

void save_telegram_state() {
    SAVE_MEMBER(EMSbus, last_bus_activity_)
    SAVE_MEMBER(EMSbus, bus_uptime_start_)
    SAVE_MEMBER(EMSbus, bus_connected_)
    SAVE_MEMBER(EMSbus, ems_mask_)
    SAVE_MEMBER(EMSbus, ems_bus_id_)
    SAVE_MEMBER(EMSbus, tx_mode_)
    SAVE_MEMBER(EMSbus, tx_state_)
    SAVE_MEMBER(EMSbus, isEMS2_)
    SAVE_MEMBER(EMSESP, watch_id_)
    SAVE_MEMBER(EMSESP, watch_)
    SAVE_MEMBER(EMSESP, read_id_)
    SAVE_MEMBER(EMSESP, read_next_)
    SAVE_MEMBER(EMSESP, publish_id_)
    SAVE_MEMBER(EMSESP, response_id_)
    SAVE_MEMBER(EMSESP, tap_water_active_)
    SAVE_MEMBER(EMSESP, publish_all_idx_)
    SAVE_MEMBER(EMSESP, unique_id_count_)
    SAVE_MEMBER(EMSESP, trace_raw_)
    SAVE_MEMBER(EMSESP, wait_validate_)
    SAVE_MEMBER(EMSESP, wait_km_)
    SAVE_MEMBER(EMSESP, last_fetch_)
    SAVE_MEMBER(EMSESP, last_entity_change_)
    SAVE_MEMBER(EMSESP, entity_compaction_pending_)
    SAVE_MEMBER(Roomctrl, switch_off_)
    SAVE_MEMBER(Roomctrl, send_time_)
    SAVE_MEMBER(Roomctrl, receive_time_)
    SAVE_MEMBER(Roomctrl, remotetemp_)
    SAVE_MEMBER(Roomctrl, remotehum_)
    SAVE_MEMBER(Roomctrl, sendtype_)
    SAVE_MEMBER(Roomctrl, type_)
    SAVE_MEMBER(Roomctrl, timeout_)
}

// The custom entities' values as loaded at start-up. WebCustomEntityService::get_value() (called for every
// telegram) writes value / data / raw of the entities that match, so they are put back with the rest.
struct SavedEntity {
    uint32_t             value;
    std::string          data;
    std::vector<uint8_t> raw;
};
std::vector<SavedEntity> saved_entities;

void save_custom_entities() {
    EMSESP::webCustomEntityService.read([](WebCustomEntity & w) {
        for (const auto & e : w.customEntityItems) {
            SavedEntity s{e.value, e.data.c_str(), {}};
            if (e.raw) {
                s.raw.assign(e.raw, e.raw + static_cast<size_t>(e.factor) + 1);
            }
            saved_entities.push_back(std::move(s));
        }
    });
}

void restore_custom_entities() {
    EMSESP::webCustomEntityService.read([](WebCustomEntity & w) {
        auto s = saved_entities.begin();
        for (auto & e : w.customEntityItems) {
            if (s == saved_entities.end()) {
                break;
            }
            e.value = s->value;
            e.data  = s->data.c_str();
            if (e.raw) {
                memcpy(e.raw, s->raw.data(), s->raw.size());
            }
            ++s;
        }
    });
}

AsyncWebServer * webServer;
ESP32React *     esp32React;
EMSESP           application;
FS               dummyFS;

// a telegram without CRC, as in uart_telegram() of test_api.cpp
void uart_telegram(const std::vector<uint8_t> & rx_data) {
    uint8_t data[EMS_MAX_TELEGRAM_LENGTH];
    uint8_t len = rx_data.size();
    for (uint8_t i = 0; i < len; i++) {
        data[i] = rx_data[i];
    }
    data[len] = EMSESP::rxservice_.calculate_crc(data, len);
    EMSESP::incoming_telegram(data, len + 1);
}

// as add_device() in test_api.cpp: the device answers a version request with its product id
void add_device(uint8_t device_id, uint8_t product_id) {
    uart_telegram({device_id, 0x49, EMSdevice::EMS_TYPE_VERSION, 0, product_id, 1, 0});
}

void add_devices() {
    // boiler, with the telegrams test_api.cpp uses
    add_device(0x08, 123); // Nefit Trendline
    uart_telegram({0x08, 0x49, 0x14, 00, 0x3C, 0x1F, 0xAC, 0x70});
    uart_telegram({0x08, 0x00, 0x18, 0x00, 0x00, 0x02, 0x5A, 0x73, 0x3D, 0x0A, 0x10, 0x65, 0x40, 0x02, 0x1A,
                   0x80, 0x00, 0x01, 0xE1, 0x01, 0x76, 0x0E, 0x3D, 0x48, 0x00, 0xC9, 0x44, 0x02, 0x00});
    uart_telegram({0x08, 0x49, 0x33, 0x00, 0x08, 0xFF, 0x34, 0xFB, 0x00, 0x28, 0x00, 0x00, 0x46, 0x00, 0xFF, 0xFF, 0x00});

    // thermostat FW120, heating circuits 1-3
    add_device(0x10, 192);
    uart_telegram({0x90, 0x00, 0xFF, 0x00, 0x00, 0x6F, 0x03, 0x02, 0x00, 0xCD, 0x00, 0xE4});
    uart_telegram({0x90, 0x00, 0xFF, 0x00, 0x00, 0x70, 0x02, 0x01, 0x00, 0xCE, 0x00, 0xE5});
    uart_telegram({0x90, 0x00, 0xFF, 0x00, 0x00, 0x71, 0x01, 0x02, 0x00, 0xCF, 0x00, 0xE6});

    // one more device of each remaining kind, so their telegram handlers are registered too
    add_device(0x09, 68);  // controller BC10
    add_device(0x11, 71);  // switch WM10
    add_device(0x12, 74);  // alert EM10
    add_device(0x15, 243); // extension EM100
    add_device(0x20, 69);  // mixer MM10
    add_device(0x30, 73);  // solar SM10
    add_device(0x38, 252); // heat pump module
    add_device(0x41, 100); // water IPM
    add_device(0x51, 231); // ventilation
    add_device(0x5A, 204); // pool MP100
    add_device(0x60, 228); // alternative heat source AM200
    add_device(0x4B, 189); // gateway KM200

    EMSESP::rxservice_.loop();
}

// Put the application back to the state it has right after start-up: no devices, empty Rx/Tx queues
// and counters, bus, room-controller and custom-entity state as saved, system status normal, then the
// same devices as the first time.
void reset_state() {
    EMSESP::system_.systemStatus(SYSTEM_STATUS::SYSTEM_STATUS_NORMAL);
    restore_custom_entities();
    EMSESP::clear_all_devices();
    for (const auto & m : saved_members) {
        memcpy(m.addr, m.bytes, m.size);
    }
    EMSESP::rxservice_.~RxService();
    new (&EMSESP::rxservice_) RxService();
    EMSESP::txservice_.~TxService();
    new (&EMSESP::txservice_) TxService();
    EMSESP::txservice_.start();
    add_devices();
}

} // namespace

void emsesp_env_init() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    webServer     = new AsyncWebServer(80);
    esp32React    = new ESP32React(webServer, &dummyFS);

    Serial.begin(115200);
    application.start();

    EMSESP::webCustomEntityService.load_test_data();
    EMSESP::webCustomizationService.load_test_data();
    EMSESP::temperaturesensor_.load_test_data();
    EMSESP::webSchedulerService.load_test_data();

    save_telegram_state();
    save_custom_entities();
    reset_state();
}

void emsesp_env_reset() {
    reset_state();
}

void emsesp_env_rx_telegram(const uint8_t * body, size_t length) {
    reset_state();

    // the same-size heap copy keeps ASan's bounds exact for the telegram the library is handed
    uint8_t * buf = static_cast<uint8_t *>(malloc(length + 1));
    memcpy(buf, body, length);
    buf[length] = EMSESP::rxservice_.calculate_crc(buf, length);
    EMSESP::incoming_telegram(buf, length + 1);
    free(buf);
    EMSESP::rxservice_.loop();
}

bool emsesp_env_api(const char * url, const char * body) {
    // what WebAPIService hands to the command dispatcher: a GET has an empty input object, a POST body
    // that is a JSON object becomes the input, anything else becomes input["data"]
    JsonDocument input_doc;
    JsonObject   input;
    JsonDocument body_doc;
    if (body != nullptr) {
        if (deserializeJson(body_doc, body) != DeserializationError::Ok || !body_doc.is<JsonObject>()) {
            return false; // plain text would arrive as input["data"], a value to write
        }
        input = body_doc.as<JsonObject>();
    } else {
        input = input_doc.to<JsonObject>();
    }

    // Command::process() takes the value only from input["data"] / input["value"]; without one every
    // command is a query (Command::call() with an empty value)
    if (input["data"].is<JsonVariantConst>() || input["value"].is<JsonVariantConst>()) {
        return false;
    }

    // not admin, like an unauthenticated request: the commands that act on the system (send, fetch, restart,
    // format, ...) and the user-defined commands are ADMIN_ONLY and are refused
    JsonDocument output_doc;
    JsonObject   output = output_doc.to<JsonObject>();
    Command::process(url, false, input, output);
    return true;
}
