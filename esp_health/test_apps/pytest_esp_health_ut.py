# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT

import pytest
from pytest_embedded_idf import IdfDut


@pytest.mark.parametrize('target', ['esp32s3'], indirect=True)
@pytest.mark.timeout(600)
def test_esp_health(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(timeout=5 * 60)
