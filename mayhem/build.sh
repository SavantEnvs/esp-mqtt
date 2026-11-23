#!/usr/bin/env bash
# mayhem/build.sh — build esp-mqtt's fuzz targets and its known-answer selftest.
#
# esp-mqtt is an ESP-IDF component (idf_component_register), so there is no standalone build to
# drive; the component's C sources are compiled directly against host stand-ins for the ESP-IDF
# services it uses (mayhem/esp_stubs: FreeRTOS, esp_event, tcp_transport, esp_log, sdkconfig) and
# the URL parser ESP-IDF bundles (nodejs/http-parser v2.9.4, vendored in mayhem/esp_stubs/http_parser).
# Outputs: build-mayhem/<target>, build-mayhem/<target>-standalone, build-mayhem/selftest
# (build-mayhem/ is covered by upstream's `**/build*` .gitignore rule).
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=$(cd "$(dirname "$0")/.." && pwd)}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OUT="$SRC/build-mayhem"
mkdir -p "$OUT/fuzz" "$OUT/test"

# The component's sources; the globs also match older layouts (lib/mqtt_msg.c before lib/mqtt_msg/).
shopt -s nullglob
LIB_SRCS=(mqtt_client.c mqtt5_client.c lib/platform.c lib/mqtt_outbox.c
          lib/mqtt_msg/*.c lib/mqtt_msg.c lib/mqtt5_msg.c lib/mqtt_utils/*.c
          mayhem/esp_stubs/stubs.c mayhem/esp_stubs/http_parser/http_parser.c)
LIB_SRCS=($(for s in "${LIB_SRCS[@]}"; do [ -f "$SRC/$s" ] && echo "$s"; done))
INCS=(-I"$SRC/mayhem/esp_stubs/include" -I"$SRC/mayhem/esp_stubs/http_parser" -I"$SRC/include" -I"$SRC/lib/include")
for d in test/host/mocks/include lib/mqtt_msg/include lib/mqtt_utils/include; do
    [ -d "$SRC/$d" ] && INCS+=(-I"$SRC/$d")
done
shopt -u nullglob

# compile <outdir> <flags...> -- objects for every library source into <outdir>
compile_lib() {
    local out="$1"; shift
    local objs=()
    for s in "${LIB_SRCS[@]}"; do
        local o="$out/$(echo "$s" | tr '/' '_').o"
        objs+=("$o")
        "$CC" "$@" "${INCS[@]}" -c "$SRC/$s" -o "$o" &
        while [ "$(jobs -rp | wc -l)" -ge "$MAYHEM_JOBS" ]; do wait -n; done
    done
    wait
    printf '%s\n' "${objs[@]}"
}

# 1) the library, instrumented (ASan+UBSan, halting) with DWARF 3: once with SanCov for the libFuzzer
#    binaries, once without for the standalone reproducers (which have no SanCov runtime when
#    SANITIZER_FLAGS is empty)
mkdir -p "$OUT/standalone"
# shellcheck disable=SC2086
mapfile -t FUZZ_OBJS < <(compile_lib "$OUT/fuzz" $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link)
# shellcheck disable=SC2086
mapfile -t SA_OBJS < <(compile_lib "$OUT/standalone" $SANITIZER_FLAGS $DEBUG_FLAGS -O1)
# shellcheck disable=SC2086
"$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OUT/fuzz/lsan_off.o"
# shellcheck disable=SC2086
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/fuzz/standalone_main.o"

# 2) each harness twice: libFuzzer binary and run-once standalone reproducer
build_target() {
    local name="$1" src="$2"; shift 2
    # shellcheck disable=SC2086
    "$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link "$@" "${INCS[@]}" -c "$SRC/mayhem/$src" -o "$OUT/fuzz/$name.o"
    # shellcheck disable=SC2086
    "$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -O1 "$@" "${INCS[@]}" -c "$SRC/mayhem/$src" -o "$OUT/standalone/$name.o"
    # shellcheck disable=SC2086
    "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OUT/fuzz/$name.o" "${FUZZ_OBJS[@]}" \
        "$OUT/fuzz/lsan_off.o" -o "$OUT/$name"
    # shellcheck disable=SC2086
    "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/fuzz/standalone_main.o" "$OUT/standalone/$name.o" "${SA_OBJS[@]}" \
        "$OUT/fuzz/lsan_off.o" -o "$OUT/$name-standalone"
}
build_target fuzz_client_v5 fuzz_client.c -DFUZZ_PROTO=5
build_target fuzz_client_v311 fuzz_client.c -DFUZZ_PROTO=311
build_target fuzz_uri fuzz_uri.c
build_target fuzz_api fuzz_api.c

# 3) the known-answer selftest, with normal flags (no sanitizers) for mayhem/test.sh
# shellcheck disable=SC2086
mapfile -t TEST_OBJS < <(compile_lib "$OUT/test" -O1 $COVERAGE_FLAGS)
# shellcheck disable=SC2086
"$CC" -O1 $COVERAGE_FLAGS "${INCS[@]}" "$SRC/mayhem/selftest.c" "${TEST_OBJS[@]}" -o "$OUT/selftest"

# 4) upstream's own Catch2 host suites (what its CI runs as host_tests:*), normal flags, against the
#    same library objects. Catch2 v3 and rapidcheck (the cmake/RapidCheck.cmake pin) are prebuilt in
#    the image under /opt/toolchains/deps (mayhem/Dockerfile). test/host is NOT built: it links
#    against CMock mocks that ESP-IDF generates from its own headers ($IDF_PATH/tools/mocks for
#    FreeRTOS, esp_timer, esp_event, tcp_transport), which need a full ESP-IDF tree; mayhem/esp_stubs
#    are hand-written stand-ins, not those mocks.
DEPS=/opt/toolchains/deps
CATCH2_DIR=$(ls -d "$DEPS"/catch2-* 2>/dev/null | sort -V | tail -1)
RC_DIR=$(ls -d "$DEPS"/rapidcheck-* 2>/dev/null | tail -1)
if [ -z "$CATCH2_DIR" ] || [ -z "$RC_DIR" ]; then
    echo "build.sh: Catch2/rapidcheck missing under $DEPS (see mayhem/Dockerfile)" >&2; exit 1
fi
CATCH2_LIBDIR=$(dirname "$(ls "$CATCH2_DIR"/lib*/libCatch2.a | head -1)")
RC_LIBDIR=$(dirname "$(ls "$RC_DIR"/lib*/librapidcheck.a | head -1)")
"$CC" -O1 $COVERAGE_FLAGS -c "$SRC/mayhem/host_test_shims.c" -o "$OUT/test/host_test_shims.o"
HOST_TEST_LIBS=(-L"$CATCH2_LIBDIR" -lCatch2 -L"$RC_LIBDIR" -lrapidcheck)
build_host_test() {
    # <name> <c++ std> "<library sources the suite links, as upstream's CMake does>" <test sources...>
    local name="$1" std="$2" libs="$3"; shift 3
    local objs=() s o
    for s in $libs; do
        if [ "$s" = HOSTSHIM ]; then objs+=("$OUT/test/host_test_shims.o"); continue; fi
        objs+=("$OUT/test/$(echo "$s" | tr '/' '_').o")
    done
    for s in "$@"; do
        o="$OUT/test/${name}_$(basename "$s" .cpp).o"
        # shellcheck disable=SC2086
        "$CXX" -std="$std" -O1 $COVERAGE_FLAGS -Wno-missing-field-initializers "${INCS[@]}" \
            -I"$CATCH2_DIR/include" -I"$RC_DIR/include" -c "$SRC/$s" -o "$o" &
        objs+=("$o")
    done
    wait
    # shellcheck disable=SC2086
    "$CXX" $COVERAGE_FLAGS "${objs[@]}" "${HOST_TEST_LIBS[@]}" -o "$OUT/$name"
}
build_host_test mqtt_msg_host_test c++17 "lib/mqtt_msg/mqtt_msg.c lib/mqtt_msg/mqtt5_msg.c" \
    test/mqtt_msg_host_test/main/test_main.cpp test/mqtt_msg_host_test/main/test_mqtt_msg.cpp
build_host_test mqtt_outbox_host_test c++17 "lib/mqtt_outbox.c lib/mqtt_msg/mqtt_msg.c lib/mqtt_msg/mqtt5_msg.c lib/platform.c HOSTSHIM" \
    test/mqtt_outbox_host_test/main/test_main.cpp test/mqtt_outbox_host_test/main/test_outbox.cpp
build_host_test mqtt_utils_host_test c++20 "lib/mqtt_utils/mqtt_utils.c" \
    test/mqtt_utils_host_test/main/test_main.cpp test/mqtt_utils_host_test/main/test_cases.cpp
echo "build.sh: done"
