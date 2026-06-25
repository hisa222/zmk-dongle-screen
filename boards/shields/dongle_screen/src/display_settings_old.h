/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Display Settings - NVS Persistence (brightness only)
 *
 * Persists screen brightness settings across reboots using
 * Zephyr Settings Subsystem (NVS backend).
 *
 * NVS key:
 *   "dongle/brightness" - manual_brightness (uint8_t)
 */

#ifndef DISPLAY_SETTINGS_H
#define DISPLAY_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize the display settings subsystem.
 * Loads saved brightness from NVS flash. Call once during display init.
 */
void display_settings_init(void);

/**
 * Save settings to NVS if the value has changed since last save.
 * Call when leaving the settings screen.
 * No-op if nothing changed.
 */
void display_settings_save_if_dirty(void);

/* ========== Brightness ========== */

uint8_t display_settings_get_manual_brightness(void);
void display_settings_set_manual_brightness(uint8_t level);

#endif /* DISPLAY_SETTINGS_H */
