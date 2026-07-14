#!/bin/bash
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
# GitLab CI: push the tested revision (tag or branch) to GitHub.

set -ex

if [ -n "${CI_COMMIT_TAG}" ]; then
    git push github "${CI_COMMIT_TAG}"
else
    git push github "${CI_COMMIT_SHA}:refs/heads/${CI_COMMIT_REF_NAME}"
fi
