#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
# Shared helpers for esp-health CI.
set -euo pipefail

CI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${CI_DIR}/../.." && pwd)"
COMPONENT_ROOT="${REPO_ROOT}/esp_health"

DEFAULT_TARGETS=(esp32s3 esp32p4 esp32s31)
DEFAULT_TEST_TARGETS=(esp32s3)

die() {
    echo "ERROR: $*" >&2
    exit 1
}

require_idf() {
    [[ -n "${IDF_PATH:-}" ]] || die "IDF_PATH is not set; run . \$IDF_PATH/export.sh first"
    command -v idf.py >/dev/null 2>&1 || die "idf.py not found in PATH"
}

resolve_targets() {
    if [[ -n "${TARGETS:-}" ]]; then
        # shellcheck disable=SC2206
        TARGETS_ARR=(${TARGETS})
    elif [[ -n "${BUILD_TARGET_CHIPS:-}" ]]; then
        # shellcheck disable=SC2206
        TARGETS_ARR=(${BUILD_TARGET_CHIPS})
    elif [[ -n "${IDF_TARGET:-}" ]]; then
        TARGETS_ARR=("${IDF_TARGET}")
    else
        TARGETS_ARR=("${DEFAULT_TARGETS[@]}")
    fi
}

resolve_test_targets() {
    if [[ -n "${TEST_TARGETS:-}" ]]; then
        # shellcheck disable=SC2206
        TARGETS_ARR=(${TEST_TARGETS})
    elif [[ -n "${IDF_TARGET:-}" ]]; then
        TARGETS_ARR=("${IDF_TARGET}")
    else
        TARGETS_ARR=("${DEFAULT_TEST_TARGETS[@]}")
    fi
}

idf_preview_flags() {
    local target="$1"
    local t
    # shellcheck disable=SC2206
    local preview_list=(${EXTRA_PREVIEW_TARGETS:-esp32h4 esp32s31})
    for t in "${preview_list[@]}"; do
        if [[ -n "${t}" && "${t}" == "${target}" ]]; then
            echo "--preview"
            return 0
        fi
    done
}

idf_py() {
    local target="$1"
    shift
    local preview
    preview="$(idf_preview_flags "${target}")"
    if [[ -n "${preview}" ]]; then
        idf.py "${preview}" "$@"
    else
        idf.py "$@"
    fi
}

list_examples() {
    local d
    [[ -d "${COMPONENT_ROOT}/examples" ]] || die "Missing examples dir: ${COMPONENT_ROOT}/examples"
    for d in "${COMPONENT_ROOT}/examples"/*/; do
        [[ -f "${d}/CMakeLists.txt" ]] || continue
        echo "${d%/}"
    done
}

# Customer board whose board_info.yaml chip matches $target. Empty if none.
example_board_for_target() {
    local proj="$1"
    local target="$2"
    local info chip
    local customer="${proj}/components/board_customer"
    [[ -d "${customer}" ]] || return 0
    for info in "${customer}"/*/board_info.yaml; do
        [[ -f "${info}" ]] || continue
        chip="$(sed -n 's/^chip:[[:space:]]*"\{0,1\}\([^"#]*\).*/\1/p' "${info}" | head -1 | tr -d '[:space:]')"
        if [[ "${chip}" == "${target}" ]]; then
            basename "$(dirname "${info}")"
            return 0
        fi
    done
}

# CI sets IDF_VERSION to a git ref (e.g. release/v6.1) for checkout. That value
# breaks idf-component-manager / esp-bmgr-assist (expects numeric 6.1.x).
sanitize_idf_version_env() {
    if [[ -z "${IDF_VERSION:-}" ]]; then
        return 0
    fi
    if [[ "${IDF_VERSION}" =~ ^[0-9]+(\.[0-9]+)*([.-].*)?$ ]]; then
        return 0
    fi
    local ver_cmake="${IDF_PATH}/tools/cmake/version.cmake"
    local major="" minor="" patch=""
    if [[ -n "${IDF_PATH:-}" && -f "${ver_cmake}" ]]; then
        major="$(sed -n 's/.*set([[:space:]]*IDF_VERSION_MAJOR[[:space:]]\+\([0-9]\+\).*/\1/p' "${ver_cmake}" | head -1)"
        minor="$(sed -n 's/.*set([[:space:]]*IDF_VERSION_MINOR[[:space:]]\+\([0-9]\+\).*/\1/p' "${ver_cmake}" | head -1)"
        patch="$(sed -n 's/.*set([[:space:]]*IDF_VERSION_PATCH[[:space:]]\+\([0-9]\+\).*/\1/p' "${ver_cmake}" | head -1)"
    fi
    if [[ -n "${major}" && -n "${minor}" && -n "${patch}" ]]; then
        echo "NOTE: IDF_VERSION was '${IDF_VERSION}' (git ref); using ${major}.${minor}.${patch} for component manager"
        export IDF_VERSION="${major}.${minor}.${patch}"
    else
        echo "NOTE: unsetting non-numeric IDF_VERSION='${IDF_VERSION}' for component manager"
        unset IDF_VERSION
    fi
}

# main REQUIRES gen_bmgr_codes; create a tiny stub so set-target can configure
# and download esp_board_manager before real `idf.py bmgr` overwrites it.
ensure_gen_bmgr_stub() {
    local gen_dir="components/gen_bmgr_codes"
    if [[ -f "${gen_dir}/CMakeLists.txt" ]]; then
        return 0
    fi
    mkdir -p "${gen_dir}"
    cat > "${gen_dir}/CMakeLists.txt" <<'EOF'
# Stub for first configure; overwritten by `idf.py bmgr`.
idf_component_register(SRCS "stub.c" INCLUDE_DIRS ".")
EOF
    cat > "${gen_dir}/stub.c" <<'EOF'
/* Placeholder; replaced when idf.py bmgr generates board sources. */
void esp_health_bmgr_stub(void) {}
EOF
}

# Build an IDF project into build_<target>.
# Args: project_dir target [bmgr]
# Pass bmgr=1 for examples (same as README):
#   stub gen_bmgr_codes → set-target → idf.py bmgr → build
# Examples are skipped when board_customer has no chip matching $target
# (reference board is ESP32-S3 DevKit + MAX30102).
build_idf_project() {
    local proj="$1"
    local target="$2"
    local do_bmgr="${3:-0}"
    local board=""

    [[ -d "${proj}" ]] || die "Missing project: ${proj}"
    [[ -f "${proj}/CMakeLists.txt" ]] || die "Not an IDF project: ${proj}"

    if [[ "${do_bmgr}" == "1" ]]; then
        board="$(example_board_for_target "${proj}" "${target}")"
        if [[ -z "${board}" ]]; then
            echo "SKIP $(basename "${proj}") / ${target}: no board_customer for this chip"
            return 0
        fi
    fi

    echo "-------- $(basename "${proj}") / ${target} --------"
    (
        cd "${proj}"
        rm -rf "build_${target}"
        export IDF_TARGET="${target}"
        sanitize_idf_version_env

        if [[ "${do_bmgr}" == "1" ]]; then
            [[ -d components/board_customer ]] || die "${proj}: missing components/board_customer"
            [[ -d components/amend/max30102 ]] || die "${proj}: missing components/amend/max30102"
            ensure_gen_bmgr_stub
        fi

        # set-target first: CMake downloads managed_components (esp_board_manager).
        idf_py "${target}" -B "build_${target}" set-target "${target}"

        if [[ "${do_bmgr}" == "1" ]]; then
            echo "bmgr: board=${board}"
            idf.py bmgr -b "${board}" \
                -c components/board_customer \
                -a components/amend/max30102
            [[ -f components/gen_bmgr_codes/CMakeLists.txt ]] \
                || die "${proj}: bmgr did not create components/gen_bmgr_codes"
            # Drop stub-only leftover if bmgr regenerated without stub.c
            rm -f components/gen_bmgr_codes/stub.c
        fi

        idf_py "${target}" -B "build_${target}" build
    )
    echo "OK: $(basename "${proj}") / ${target}"
}
