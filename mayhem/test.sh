#!/usr/bin/env bash
# mayhem/test.sh — run what mayhem/build.sh built for testing, never compiling here:
#  * upstream's Catch2 host suites (test/mqtt_msg_host_test, test/mqtt_outbox_host_test,
#    test/mqtt_utils_host_test; upstream CI's host_tests:* jobs), one CTRF test per Catch2 TEST_CASE,
#    with Catch2's and rapidcheck's seeds pinned so a run is reproducible. test/host is not built
#    (needs ESP-IDF's CMock mocks; see mayhem/build.sh).
#  * our known-answer selftest (build-mayhem/selftest), which esp-mqtt's public client API against scripted broker streams and asserts the exact
# CONNECT/SUBSCRIBE bytes written and the decoded CONNACK/PUBLISH/v5-property events.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=$(cd "$(dirname "$0")/.." && pwd)}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

PASSED=0; FAILED=0

# Upstream's Catch2 suites. Catch2 ends with either
#   "All tests passed (N assertions in M test cases)"  or
#   "test cases: T | P passed | F failed"  (optionally "| S skipped").
# A missing binary, a missing summary line or a non-zero exit with no counted failure is a failure.
export RC_PARAMS="seed=1"
for suite in mqtt_msg_host_test mqtt_outbox_host_test mqtt_utils_host_test; do
  bin="$SRC/build-mayhem/$suite"
  if [ ! -x "$bin" ]; then
    echo "test.sh: $bin missing — run mayhem/build.sh first" >&2
    FAILED=$(( FAILED + 1 )); continue
  fi
  out="$("$bin" --rng-seed 1 2>&1)"; rc=$?
  echo "$out"
  p=0; f=0
  if line=$(grep -E '^All tests passed \(.* in [0-9]+ test cases?\)' <<<"$out" | tail -1); then
    p=$(sed -E 's/.* in ([0-9]+) test cases?\).*/\1/' <<<"$line")
  elif line=$(grep -E '^test cases: *[0-9]+' <<<"$out" | tail -1); then
    p=$(grep -oE '[0-9]+ passed' <<<"$line" | grep -oE '[0-9]+' || echo 0)
    f=$(grep -oE '[0-9]+ failed' <<<"$line" | grep -oE '[0-9]+' || echo 0)
  else
    echo "test.sh: $suite printed no Catch2 summary" >&2; f=1
  fi
  [ "$rc" -ne 0 ] && [ "$f" -eq 0 ] && f=1
  [ "$p" -eq 0 ] && [ "$f" -eq 0 ] && f=1   # an empty run is not a pass
  echo "test.sh: $suite: $p passed, $f failed"
  PASSED=$(( PASSED + p )); FAILED=$(( FAILED + f ))
done

# Our known-answer selftest: every expected case must report PASS; a missing report is a failure.
EXPECTED=11
bin="$SRC/build-mayhem/selftest"
if [ ! -x "$bin" ]; then
  echo "test.sh: $bin missing — run mayhem/build.sh first" >&2
  FAILED=$(( FAILED + EXPECTED ))
else
  out="$("$bin" 2>&1)"; rc=$?
  echo "$out"
  passed=$(grep -c '^PASS ' <<<"$out" || true)
  failed=$(grep -c '^FAIL ' <<<"$out" || true)
  missing=$(( EXPECTED - passed - failed ))
  [ "$missing" -gt 0 ] && failed=$(( failed + missing ))
  [ "$rc" -ne 0 ] && [ "$failed" -eq 0 ] && failed=1
  PASSED=$(( PASSED + passed )); FAILED=$(( FAILED + failed ))
fi
emit_ctrf "catch2+esp-mqtt-selftest" "$PASSED" "$FAILED"
