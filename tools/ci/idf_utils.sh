# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT

function add_ssh_keys() {
  local key_string="${1}"
  mkdir -p ~/.ssh
  chmod 700 ~/.ssh
  echo -n "${key_string}" >~/.ssh/id_rsa_base64
  base64 --decode --ignore-garbage ~/.ssh/id_rsa_base64 >~/.ssh/id_rsa
  chmod 600 ~/.ssh/id_rsa
}

function gitlab_ssh_key_usable() {
  [[ -n "${GITLAB_KEY:-}" ]] || return 1
  [[ -f ~/.ssh/id_rsa ]] || return 1
  ssh-keygen -y -f ~/.ssh/id_rsa >/dev/null 2>&1
}

function add_gitlab_ssh_keys() {
  if [[ -z "${GITLAB_KEY:-}" ]]; then
    echo "ERROR GITLAB_KEY is not set."
    echo "Ask a project Maintainer to add masked CI variable GITLAB_KEY (same value as esp_led / esp-gmf group)."
    exit 1
  fi
  add_ssh_keys "${GITLAB_KEY}"
  if ! gitlab_ssh_key_usable; then
    echo "ERROR GITLAB_KEY is set but SSH private key is invalid (check base64 encoding)."
    exit 1
  fi
  # Build runners often cannot reach GitLab on port 22; geo SSH uses 27227.
  echo -e "Host gitlab.espressif.cn\n\tStrictHostKeyChecking no\n\tPort 27227\n" >>~/.ssh/config
  if [ "${LOCAL_GITLAB_SSH_SERVER:-}" ]; then
    SRV=${LOCAL_GITLAB_SSH_SERVER##*@}
    local port="${SRV##*:}"
    SRV=${SRV%%:*}
    if [ "${port}" = "${SRV}" ]; then
      port=""
    fi
    {
      printf "Host %s\n\tStrictHostKeyChecking no\n" "${SRV}"
      if [ -n "${port}" ]; then
        printf "\tPort %s\n" "${port}"
      fi
    } >>~/.ssh/config
  fi
}

function add_github_ssh_keys() {
  if [[ -z "${GH_PUSH_KEY:-}" ]]; then
    echo "ERROR GH_PUSH_KEY is not set."
    echo "Ask a project Maintainer to add masked CI variable GH_PUSH_KEY (GitHub deploy key)."
    exit 1
  fi
  add_ssh_keys "${GH_PUSH_KEY}"
  echo -e "Host github.com\n\tStrictHostKeyChecking no\n" >>~/.ssh/config
}

# Build a git clone URL from a CI SSH base (ssh://user@host:port or user@host:port).
function _idf_url_from_ssh_base() {
  local base="${1}"
  if [[ "${base}" == ssh://* ]]; then
    echo "${base%/}/espressif/esp-idf.git"
  elif [[ "${base}" == *@*:* ]]; then
    echo "ssh://${base}/espressif/esp-idf.git"
  else
    echo "${base}/espressif/esp-idf.git"
  fi
}

# Resolve esp-idf remote URL at runtime. esp-health may not inherit group CI vars.
function resolve_idf_repository() {
  local url=""
  if [[ -n "${IDF_REPOSITORY:-}" && "${IDF_REPOSITORY}" != /* && ( "${IDF_REPOSITORY}" == *@* || "${IDF_REPOSITORY}" == ssh://* ) ]]; then
    url="${IDF_REPOSITORY}"
  elif [[ -n "${IDF_GIT_URL:-}" ]]; then
    url="${IDF_GIT_URL}"
  elif [[ -n "${GITLAB_SSH_SERVER:-}" ]]; then
    url="$(_idf_url_from_ssh_base "${GITLAB_SSH_SERVER}")"
  elif [[ -n "${LOCAL_GITLAB_SSH_SERVER:-}" ]]; then
    url="$(_idf_url_from_ssh_base "${LOCAL_GITLAB_SSH_SERVER}")"
  else
    url="ssh://git@gitlab.espressif.cn:27227/espressif/esp-idf.git"
  fi
  echo "${url}"
}

function clone_idf() {
  local branch="${1:-${IDF_VERSION_TAG}}"
  local dest="${2:-${IDF_PATH}}"
  export IDF_REPOSITORY="$(resolve_idf_repository)"
  if [[ "${IDF_REPOSITORY}" != *@* && "${IDF_REPOSITORY}" != ssh://* ]]; then
    echo "ERROR invalid IDF_REPOSITORY='${IDF_REPOSITORY}'"
    exit 1
  fi
  mkdir -p "$(dirname "${dest}")"
  echo "Cloning IDF from ${IDF_REPOSITORY} branch ${branch} -> ${dest}"
  git clone --depth 1 -b "${branch}" "${IDF_REPOSITORY}" "${dest}"
}

function common_before_scripts() {
  source $IDF_PATH/tools/ci/utils.sh

  if [[ -n "${REQUIRED_ANCESTOR_COMMITS:-}" ]]; then
    is_based_on_commits $REQUIRED_ANCESTOR_COMMITS
  fi

  if [[ -n "$IDF_DONT_USE_MIRRORS" ]]; then
    export IDF_MIRROR_PREFIX_MAP=
  fi

  source $IDF_PATH/tools/ci/configure_ci_environment.sh
  export PYTHONPATH="$IDF_PATH/tools:$IDF_PATH/tools/esp_app_trace:$IDF_PATH/components/partition_table:$IDF_PATH/tools/ci/python_packages:$PYTHONPATH"
}

function setup_tools_and_idf_python_venv() {
  pushd ${IDF_PATH} 2>/dev/null

  if [[ -n "$IDF_DONT_USE_MIRRORS" ]]; then
    export IDF_MIRROR_PREFIX_MAP=
  fi

  if [[ "${CI_JOB_STAGE}" == "target_test" || "${CI_JOB_STAGE}" == "test" ]]; then
    # Board runners are small; never install every chip toolchain.
    # shellcheck disable=SC2206
    local chips=(${TEST_TARGETS:-${IDF_TARGET:-esp32s3}})
    run_cmd bash install.sh --enable-ci --enable-pytest "${chips[@]}"
  else
    run_cmd bash install.sh --enable-ci
  fi

  source ./export.sh
  popd
}

function check_idf_version() {
  local idf_ver_tag="${1}"
  if [[ "$IDF_TAG_FLAG" = "true" ]]; then
    export IDF_VERSION="${idf_ver_tag}"
  else
    if [[ -d "${IDF_PATH}" ]]; then
      pushd ${IDF_PATH} 2>/dev/null
      local idf_ver=$(git ls-remote --heads origin release/${idf_ver_tag} | grep -o "release/.*")
      if [[ -n "$idf_ver" ]]; then
        export IDF_VERSION="release/${idf_ver_tag}"
      else
        export IDF_VERSION="${idf_ver_tag}"
      fi
      popd
    else
      export IDF_VERSION="${idf_ver_tag}"
    fi
  fi
  echo "IDF_TAG_FLAG: $IDF_TAG_FLAG"
  echo "Set IDF_VERSION $IDF_VERSION"
}

function set_idf() {
  if [ -z $IDF_PATH ] || [ -z $IDF_VERSION ] ; then
    echo "Mandatory variables undefined"
    exit 1
  fi

  pushd $IDF_PATH
  git clean -f

  if [[ "$IDF_TAG_FLAG" = "true" ]]; then
    if [[ -n "$(git ls-remote origin "refs/tags/${IDF_VERSION}" 2>/dev/null)" ]]; then
      git fetch origin tag "${IDF_VERSION}" --depth 1
      git checkout "${IDF_VERSION}"
      echo "The IDF branch is TAG:${IDF_VERSION}"
    else
      git fetch origin "${IDF_VERSION}" --depth 1
      git checkout -B "${IDF_VERSION}" "origin/${IDF_VERSION}"
      echo "The IDF branch is ${IDF_VERSION} (IDF_TAG_FLAG true but no remote tag; using as branch)"
    fi
  else
    git fetch origin "${IDF_VERSION}" --depth 1
    git checkout -B "${IDF_VERSION}" "origin/${IDF_VERSION}"
    echo "The IDF branch is ${IDF_VERSION}"
  fi

  git log -1
  rm -rf $IDF_PATH/components/mqtt/esp-mqtt
  git submodule update --init --recursive --depth 1
  popd
}

function fetch_idf_branch() {
  local idf_ver="${1}"

  check_idf_version ${idf_ver}

  if [[ -n "${IDF_PATH}" ]]; then
    local idf_remote
    idf_remote="$(resolve_idf_repository)"
    pushd ${IDF_PATH}
    git init
    git clean -f
    local result=$(git remote)
    if [[ -n "$result" ]]; then
      git remote set-url origin "${idf_remote}"
    else
      git remote add origin "${idf_remote}"
    fi
    popd

    set_idf
  else
    echo "IDF_PATH not set"
    exit 1
  fi
}
