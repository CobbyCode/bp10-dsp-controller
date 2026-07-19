/*
 * SPDX-FileCopyrightText: 2026 CobbyCode
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#define ESP_RETURN_ON_ERROR(expr, tag, msg) do { (void)(tag); (void)(msg); \
    esp_err_t _err = (expr); if (_err != ESP_OK) return _err; } while (0)
