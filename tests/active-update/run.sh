#!/usr/bin/env bash
set -euo pipefail

if [[ -n ${COMBO_NATIVE_ARTIFACTS:-} && ( -e "$COMBO_NATIVE_ARTIFACTS" || -L "$COMBO_NATIVE_ARTIFACTS" ) ]]; then
    echo "Use a fresh COMBO_NATIVE_ARTIFACTS directory; existing evidence is preserved" >&2
    exit 2
fi
owner=$(cd "$(dirname "$0")/../.." && pwd)
workspace=${1:-$(west topdir)}
zmk_source=$(cd "$workspace" && west list zmk -f '{abspath}')
custom=$(cd "$workspace" && west list zmk-feature-custom-settings -f '{abspath}')
build=$(mktemp -d)
trap 'rm -rf -- "$build"' EXIT
if [[ -n ${ZEPHYR_MODULES:-} ]]; then
    export ZEPHYR_MODULES="$ZEPHYR_MODULES;$owner/tests/active-update/module"
fi
west build -s "$zmk_source/app" -b native_sim//zmk_test_mock -d "$build" -p -- \
    -DCONFIG_ASSERT=y -DZMK_CONFIG="$owner/tests/active-update" \
    -DZMK_EXTRA_MODULES="$owner;$custom;$owner/tests/active-update/module" 2>&1 | tee "$build/build.log"
status=0
for run in 1 2 3; do
    set +e
    timeout 15 "$build/zephyr/zmk.exe" >"$build/run-$run.log" 2>&1
    result=$?
    set -e
    cat "$build/run-$run.log"
    if ! python3 "$owner/tests/sanitizer_diagnostics.py" "$build/run-$run.log"; then status=1; fi
    if [[ $result != 0 ]] || ! grep -q 'runtime combo active-update: PASS groups=12' "$build/run-$run.log" ||
        ! grep -q 'runtime combo pending: PASS groups=10' "$build/run-$run.log" ||
        ! grep -q 'runtime combo review: PASS groups=9' "$build/run-$run.log" ||
        ! grep -q 'runtime combo replay: PASS groups=2' "$build/run-$run.log" ||
        ! grep -q 'runtime combo active storage: PASS groups=2' "$build/run-$run.log" ||
        ! grep -q 'runtime combo release reentry: PASS groups=1' "$build/run-$run.log" ||
        ! grep -q 'runtime combo hold-tap binding: PASS groups=2' "$build/run-$run.log"; then status=1; fi
done
if [[ -n ${COMBO_NATIVE_ARTIFACTS:-} ]]; then
    mkdir -p "$COMBO_NATIVE_ARTIFACTS"
    cp "$build"/run-*.log "$build/build.log" "$build/CMakeCache.txt" \
        "$build/zephyr/.config" "$build/zephyr/zephyr.dts" "$build/zephyr/zmk.exe" "$COMBO_NATIVE_ARTIFACTS/"
    if [[ -f "$build/zephyr_modules.txt" ]]; then cp "$build/zephyr_modules.txt" "$COMBO_NATIVE_ARTIFACTS/"; fi
fi
exit "$status"
