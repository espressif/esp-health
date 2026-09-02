#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
# Flash and run test_apps Unity cases on-target (pytest-embedded).
# Uses prebuilt artifacts from the build job; does not compile on the board runner.
#
# Usage:
#   ./tools/ci/test.sh
#   TEST_TARGETS=esp32s3 ./tools/ci/test.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

[[ -n "${IDF_PATH:-}" ]] || die "IDF_PATH is not set"
resolve_test_targets

TEST_DIR="${COMPONENT_ROOT}/test_apps"
[[ -d "${TEST_DIR}" ]] || die "Missing ${TEST_DIR}"
[[ -f "${TEST_DIR}/pytest_esp_health_ut.py" ]] || die "Missing pytest_esp_health_ut.py"

# Image already has pytest-embedded; do not --upgrade (2.9.x breaks jtag/qemu 2.8.1).
pip install pytest pytest-embedded pytest-embedded-idf \
    pytest-embedded-serial-esp pytest-rerunfailures pytest-timeout idf_build_apps

echo "==> On-target test: ${TARGETS_ARR[*]}"

for target in "${TARGETS_ARR[@]}"; do
    echo ""
    echo "======== pytest test_apps / ${target} ========"
    build_dir="build_${target}"
    if [[ ! -f "${TEST_DIR}/${build_dir}/flasher_args.json" ]]; then
        die "No ${build_dir} flash artifacts. The build job must produce them first."
    fi
    (
        cd "${TEST_DIR}"
        pytest pytest_esp_health_ut.py \
            --embedded-services esp,idf \
            --target "${target}" \
            --build-dir "${build_dir}" \
            --junitxml="${REPO_ROOT}/XUNIT_RESULT.xml"
    )
done

echo ""
echo "==> Test done"
