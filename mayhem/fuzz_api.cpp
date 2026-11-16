// Fuzzes the REST API dispatcher behind /api/... (Command::process -> device / system / custom entity /
// scheduler / sensor commands, and the JSON they render), with the request path and body an HTTP client
// would send. The WebAPIService glue itself is not called: the lib_standalone web server mock never frees
// the response that parse() hands to send(), so every call would leak its JSON output until the process
// runs out of memory.
//
// Input: a request line "/api/<path>[?query]" optionally followed by '\n' and a POST body, which must be a
// JSON object ({"cmd":"info","hc":1}); the library reads device / entity / cmd / hc ... from it. The request
// line and the body end at a NUL, as the C strings they are handed to the library as.
//
// Queries only: Command::process() takes a value to write only from the body's "data" / "value" key, and
// WebAPIService hands a GET an empty input. A request with such a key, or with a body that is not a JSON
// object (WebAPIService turns that into input["data"]), is rejected, so that no input changes settings,
// custom entities, the scheduler, sensor names or device values, and every result depends on its own
// request. emsesp_env_api() also dispatches as non-admin, so the ADMIN_ONLY commands cannot act either.
//
// State: the application is initialised once; nothing an input can reach is written to.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "emsesp_env.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    emsesp_env_init();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
    const char * nl = static_cast<const char *>(memchr(data, '\n', size));
    std::string  url(reinterpret_cast<const char *>(data), nl ? static_cast<size_t>(nl - reinterpret_cast<const char *>(data)) : size);
    // the web server hands over the path without the query string
    const std::string path = url.substr(0, url.find('?'));

    if (nl == nullptr) {
        return emsesp_env_api(path.c_str(), nullptr) ? 0 : -1;
    }
    const std::string body(nl + 1, size - (url.size() + 1));
    return emsesp_env_api(path.c_str(), body.c_str()) ? 0 : -1;
}
