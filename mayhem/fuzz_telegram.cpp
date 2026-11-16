// Fuzzes the EMS bus receive path: RxService::add() (length / CRC / queueing), then the telegram
// parser and every registered device's handlers once the queue is drained (EMSESP::rxservice_.loop()).
//
// Input: one EMS telegram without its CRC (src, dest, type id, offset, data...). The CRC is appended
// by emsesp_env_rx_telegram() so the fuzzer is not stopped at the checksum gate.
//
// State: before every telegram emsesp_env_rx_telegram() puts the application back to its start-up state
// (the devices are destroyed and registered again, the Rx/Tx services are re-created, the EMSbus, EMSESP
// and Roomctrl members and the custom entities' value/data/raw are restored), so a telegram that registers
// a device or changes values has no effect on the next input. What is not restored: function-local statics
// inside upstream's device handlers (the saved copy of the last vacation telegram, vacation_telegram[57],
// in thermostat.cpp, which merges bytes of earlier telegrams into later parses; lastCodeDate_ in boiler.cpp
// and thermostat.cpp, which gates whether an error code is processed), the log buffer, settings and the
// uptime clock. The statics can change which branch a later input takes, so a crash found here may not
// reproduce from its single input.

#include <cstddef>
#include <cstdint>

#include "emsesp_env.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    emsesp_env_init();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
    // 4 header bytes minimum, and a full telegram (with CRC) must fit EMS_MAX_TELEGRAM_LENGTH (32)
    if (size < 4 || size > 31) {
        return -1;
    }
    emsesp_env_rx_telegram(data, size);
    return 0;
}
