#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

UNITY_SRC=/opt/toolchains/deps/unity-2.6.1/src/src
BUILD="$SRC/build/mayhem"
mkdir -p "$BUILD"

# EMS-ESP's own native build (platformio `native-test` env): the Arduino / ESP-IDF APIs are mocked by
# lib_standalone/, the web UI / OTA / telnet / modbus / OneWire sources are left out.
DEFINES=(-DEMSESP_STANDALONE -DEMSESP_TEST -DEMSESP_UNITY -DARDUINOJSON_ENABLE_ARDUINO_STRING=1
         '-DEMSESP_DEFAULT_LOCALE="en"' '-DEMSESP_DEFAULT_BOARD_PROFILE="S32"')
INCLUDES=(-I"$SRC/src" -I"$SRC/src/core" -I"$SRC/lib_standalone" -I"$SRC/lib/uuid-common/src"
          -I"$SRC/lib/uuid-console/src" -I"$SRC/lib/uuid-log/src" -I"$SRC/lib/PButton"
          -I"$SRC/lib/espMqttClient/src" -I"$SRC/lib/espMqttClient/src/Transport"
          -I"$SRC/test/test_api" -I"$SRC/test" -I"$UNITY_SRC")
WARN=(-w)

mapfile -t PROJECT_SRCS < <(find src lib lib_standalone \( -name '*.cpp' -o -name '*.c' \) \
    -not -path 'src/ESP32React/*' -not -path 'lib/uuid-telnet/*' -not -path 'lib/uuid-syslog/*' \
    -not -path 'lib/eModbus/*' -not -path 'lib/OneWire/*' -not -path 'lib/OtaUpdater/*' \
    -not -path 'src/core/ModuleLibrary.cpp' | LC_ALL=C sort)

# compile_all <objdir> <flags...> -- <sources...>   (parallel; the compiler is resolved by name via PATH)
compile_all() {
    local objdir=$1; shift
    local flags=()
    while [ "$1" != "--" ]; do flags+=("$1"); shift; done
    shift
    mkdir -p "$objdir"
    printf '%s\n' "$@" | xargs -P "$MAYHEM_JOBS" -I{} bash -c '
        src=$1; shift; objdir=$1; shift
        out="$objdir/$(echo "$src" | tr / _).o"
        case "$src" in
            *.c)  clang   -std=c17 "$@" -c "$src" -o "$out" ;;
            *)    clang++ -std=gnu++20 -include "$SRC/mayhem/compat_string.h" -fno-exceptions -fno-rtti "$@" -c "$src" -o "$out" ;;
        esac' _ {} "$objdir" "${flags[@]}"
}

# 1) the project with sanitizers + SanCov (fuzzer-no-link, unconditional), DWARF 3. src/core/main.cpp
#    (Arduino setup()/loop() and the esp32 entry glue) is replaced by mayhem/emsesp_env.cpp.
FUZZ_SRCS=()
for f in "${PROJECT_SRCS[@]}"; do
    [ "$f" = src/core/main.cpp ] || FUZZ_SRCS+=("$f")
done
FUZZ_FLAGS=(-O1 $SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS "${DEFINES[@]}" "${INCLUDES[@]}" "${WARN[@]}")
rm -rf "$BUILD/fuzz"
compile_all "$BUILD/fuzz" "${FUZZ_FLAGS[@]}" -- "${FUZZ_SRCS[@]}" mayhem/emsesp_env.cpp

# 2) each harness, linked with libFuzzer and with the run-once standalone driver; lsan off at build time
clang $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$BUILD/standalone_main.o"
clang++ $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$BUILD/lsan_off.o"
for h in fuzz_telegram fuzz_api fuzz_shuntingyard; do
    clang++ -std=gnu++20 -include "$SRC/mayhem/compat_string.h" -fno-exceptions -fno-rtti "${FUZZ_FLAGS[@]}" -c "mayhem/$h.cpp" -o "$BUILD/$h.o"
    clang++ $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$BUILD/$h.o" "$BUILD"/fuzz/*.o "$BUILD/lsan_off.o" \
        -lm -lpthread -o "$SRC/$h"
    clang++ $SANITIZER_FLAGS $DEBUG_FLAGS "$BUILD/$h.o" "$BUILD/standalone_main.o" "$BUILD"/fuzz/*.o "$BUILD/lsan_off.o" \
        -lm -lpthread -o "$SRC/$h-standalone"
done

# 3) upstream's Unity suite (test/test_api), with the project's normal flags: no sanitizers, no -gdwarf-3
TEST_FLAGS=(-Og $COVERAGE_FLAGS "${DEFINES[@]}" "${INCLUDES[@]}" "${WARN[@]}")
rm -rf "$BUILD/test"
compile_all "$BUILD/test" "${TEST_FLAGS[@]}" -- "${PROJECT_SRCS[@]}" test/test_api/test_api.cpp "$UNITY_SRC/unity.c"
clang++ $COVERAGE_FLAGS "$BUILD"/test/*.o -lm -lpthread -o "$SRC/emsesp-test"
