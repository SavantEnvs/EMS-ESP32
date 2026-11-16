// Fuzzes the scheduler / custom-entity expression evaluator: compute() (command substitution,
// embedded {"url":...} JSON, ternaries) and calculate() (tokenizer, shunting-yard, RPN evaluation).
//
// Input: the expression text. The first byte selects compute() (even) or calculate() (odd); an empty
// input is an empty expression for compute().
//
// State: the application is initialised once, because commands inside expressions resolve against it.
// compute() is not read-only: commands() dispatches every "<device>/<entity>" it finds through
// Command::process() as an admin, with an empty value (it appends "/value"). That reaches the ADMIN_ONLY
// commands that need no value, e.g. system/restart and system/format (set the system status), system/fetch
// (queues Tx read requests) and the user-defined commands. So the application is put back to its start-up
// state before every compute() input (emsesp_env_reset(): devices, Rx/Tx queues, bus state, custom
// entities, system status). calculate() never dispatches commands. The lib_standalone HTTP client mock
// never connects for a {"url":...} snippet.

#include <cstddef>
#include <cstdint>
#include <string>

#include "emsesp_env.h"
#include "shuntingYard.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    emsesp_env_init();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
    const std::string expr = size ? std::string(reinterpret_cast<const char *>(data) + 1, size - 1) : std::string();
    if (size && (data[0] & 1)) {
        (void)emsesp::calculate(expr);
    } else {
        emsesp_env_reset();
        (void)emsesp::compute(expr);
    }
    return 0;
}
