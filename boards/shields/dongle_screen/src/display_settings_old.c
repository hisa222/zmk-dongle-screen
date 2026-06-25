/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Display Settings - NVS Persistence (brightness only)
 *
 * Uses Zephyr Settings Subsystem to persist screen brightness to flash.
 * Follows the same pattern as ZMK core (rgb_underglow.c, endpoints.c).
 *
 * NVS key:
 *   "dongle/brightness" - manual_brightness (uint8_t)
 */

#include "display_settings.h"

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(display_settings, CONFIG_ZMK_LOG_LEVEL);

/* ========== Default Values ========== */

/*
 * Default brightness matches DONGLE_SCREEN_DEFAULT_BRIGHTNESS Kconfig value.
 * Using the numeric default (80) to avoid a hard dependency on that symbol.
 * You can change this value or #ifdef it against CONFIG_DONGLE_SCREEN_DEFAULT_BRIGHTNESS.
 */
static uint8_t manual_brightness =
#ifdef CONFIG_DONGLE_SCREEN_DEFAULT_BRIGHTNESS
    CONFIG_DONGLE_SCREEN_DEFAULT_BRIGHTNESS;
#else
    80;
#endif

static bool settings_loaded = false;

/* ========== Settings Load Handler ========== */

#if IS_ENABLED(CONFIG_SETTINGS)

static int display_settings_handle_set(const char *name, size_t len,
                                       settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    int rc;

    if (settings_name_steq(name, "brightness", &next) && !next) {
        if (len != sizeof(manual_brightness)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &manual_brightness, sizeof(manual_brightness));
        if (rc >= 0) {
            LOG_INF("Loaded brightness: %d", manual_brightness);
            return 0;
        }
        return rc;
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(dongle_display, "dongle", NULL,
                               display_settings_handle_set, NULL, NULL);

/* ========== Dirty Flag Save ========== */

static bool dirty = false;

static void do_save(void) {
    settings_save_one("dongle/brightness", &manual_brightness, sizeof(manual_brightness));
    dirty = false;
    LOG_INF("Display settings saved to NVS (brightness=%d)", manual_brightness);
}

static void mark_dirty(void) {
    if (settings_loaded) {
        dirty = true;
    }
}

#else /* !CONFIG_SETTINGS */

static void mark_dirty(void) {}

#endif /* CONFIG_SETTINGS */

/* ========== Initialization ========== */

void display_settings_init(void) {
    if (settings_loaded) {
        return;
    }

#if IS_ENABLED(CONFIG_SETTINGS)
    settings_load_subtree("dongle");
#endif

    settings_loaded = true;
    LOG_INF("Display settings initialized: brightness=%d", manual_brightness);
}

void display_settings_save_if_dirty(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    if (!settings_loaded || !dirty) {
        return;
    }
    do_save();
#endif
}

/* ========== Brightness Getters/Setters ========== */

uint8_t display_settings_get_manual_brightness(void) {
    return manual_brightness;
}

void display_settings_set_manual_brightness(uint8_t level) {
    if (manual_brightness == level) {
        return;
    }
    manual_brightness = level;
    mark_dirty();
}
