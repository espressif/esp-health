#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
# 1) Compile test_apps for chips in BUILD_TARGET_CHIPS
# 2) Examples: only chips with a matching board_customer (see common.sh)
#    stub gen_bmgr_codes → set-target → idf.py bmgr → build
#
# Usage:
#   ./tools/ci/build.sh
#   TARGETS="esp32s3" ./tools/ci/build.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_idf
resolve_targets

mapfile -t EXAMPLE_LIST < <(list_examples)
[[ ${#EXAMPLE_LIST[@]} -gt 0 ]] || die "No examples to build"
[[ -d "${COMPONENT_ROOT}/test_apps" ]] || die "Missing ${COMPONENT_ROOT}/test_apps/"

echo "==> Unity/example targets: ${TARGETS_ARR[*]}"
echo "==> examples: ${EXAMPLE_LIST[*]}"

if command -v pip >/dev/null 2>&1; then
    pip install -U esp-bmgr-assist
fi

for target in "${TARGETS_ARR[@]}"; do
    echo ""
    echo "======== build / ${target} ========"
    prebuilt="${COMPONENT_ROOT}/lib/${target}/libesp_health.a"
    [[ -f "${prebuilt}" ]] || die "Missing prebuilt library: ${prebuilt}"
    build_idf_project "${COMPONENT_ROOT}/test_apps" "${target}" 0
    for example in "${EXAMPLE_LIST[@]}"; do
        build_idf_project "${example}" "${target}" 1
    done
done

echo ""
echo "==> Build done"
