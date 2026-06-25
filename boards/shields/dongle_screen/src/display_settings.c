/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * display_settings.c
 *
 * brightness 値の保持とバックライト反映を担当する。
 * Prospector 側の「表示設定の状態管理」を dongle_screen 側へ寄せるための層。
 *
 * 方針:
 * - brightness の正規値は 0..100 (%)
 * - set 時に即座にバックライトへ反映
 * - 保存が使える環境なら settings へ保存
 * - 保存機構が無くてもコンパイルできるようにしておく
 */

#include "display_settings.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#if __has_include(<zephyr/settings/settings.h>)
#include <zephyr/settings/settings.h>
#define DONGLE_SCREEN_HAS_SETTINGS 1
#else
#define DONGLE_SCREEN_HAS_SETTINGS 0
#endif

/*
 * 既存の backlight 実装に合わせて、必要ならここを差し替えてください。
 *
 * もし既に dongle_screen 側に
 *   int dongle_screen_set_backlight_percent(uint8_t percent);
 * のような関数があるなら、それを extern 宣言して呼ぶのが理想です。
 *
 * 今回は汎用移植のため weak fallback を用意しています。
 */
__attribute__((weak)) int dongle_screen_set_backlight_percent(uint8_t percent)
{
    ARG_UNUSED(percent);
    /*
     * 既存バックライト制御関数がある場合は、プロジェクト側で
     * strong symbol を定義してこちらを置き換えてください。
     */
    return 0;
}

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define DISPLAY_SETTINGS_KEY "dongle_screen/brightness"
#define DISPLAY_BRIGHTNESS_DEFAULT 60U
#define DISPLAY_BRIGHTNESS_MIN      0U
#define DISPLAY_BRIGHTNESS_MAX    100U

static uint8_t g_brightness = DISPLAY_BRIGHTNESS_DEFAULT;
static bool g_initialized = false;

/* ================================================================== */
/* Helpers                                                            */
/* ================================================================== */

static uint8_t clamp_brightness(uint8_t v)
{
    if (v > DISPLAY_BRIGHTNESS_MAX) {
        return DISPLAY_BRIGHTNESS_MAX;
    }
    return v;
}

static int apply_brightness(uint8_t brightness)
{
    /*
     * 実際のハード反映。
     * 既存プロジェクト側で dongle_screen_set_backlight_percent() を
     * 実装していればそれが呼ばれる。
     */
    return dongle_screen_set_backlight_percent(brightness);
}

#if DONGLE_SCREEN_HAS_SETTINGS
static int brightness_settings_set(const char *name, size_t len_rd,
                                   settings_read_cb read_cb, void *cb_arg)
{
    ARG_UNUSED(name);

    if (len_rd != sizeof(g_brightness)) {
        LOG_WRN("Unexpected brightness settings length: %d", (int)len_rd);
        return -EINVAL;
    }

    ssize_t len = read_cb(cb_arg, &g_brightness, sizeof(g_brightness));
    if (len < 0) {
        LOG_ERR("Failed reading brightness setting: %d", (int)len);
        return (int)len;
    }

    g_brightness = clamp_brightness(g_brightness);
    LOG_INF("Loaded brightness from settings: %u", g_brightness);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(dongle_screen_brightness, "dongle_screen",
                               NULL, brightness_settings_set, NULL, NULL);
#endif

/* ================================================================== */
/* Public API                                                         */
/* ================================================================== */

int display_settings_init(void)
{
    int ret = 0;

    if (g_initialized) {
        return 0;
    }

#if DONGLE_SCREEN_HAS_SETTINGS
    /*
     * settings subsystem が有効ならロードを試みる。
     * 失敗してもデフォルト値で継続。
     */
    ret = settings_load_subtree("dongle_screen");
    if (ret) {
        LOG_WRN("settings_load_subtree(dongle_screen) failed: %d", ret);
    }
#endif

    g_brightness = clamp_brightness(g_brightness);

    ret = apply_brightness(g_brightness);
    if (ret) {
        LOG_WRN("Failed to apply initial brightness %u: %d", g_brightness, ret);
    }

    g_initialized = true;
    LOG_INF("display_settings initialized (brightness=%u)", g_brightness);
    return 0;
}

uint8_t display_settings_get_brightness(void)
{
    if (!g_initialized) {
        display_settings_init();
    }

    return g_brightness;
}

int display_settings_set_brightness(uint8_t brightness)
{
    if (!g_initialized) {
        display_settings_init();
    }

    brightness = clamp_brightness(brightness);

    if (brightness == g_brightness) {
        return 0;
    }

    g_brightness = brightness;

    int ret = apply_brightness(g_brightness);
    if (ret) {
        LOG_WRN("Failed to apply brightness %u: %d", g_brightness, ret);
    }

#if DONGLE_SCREEN_HAS_SETTINGS
    ret = settings_save_one(DISPLAY_SETTINGS_KEY, &g_brightness, sizeof(g_brightness));
    if (ret) {
        LOG_WRN("Failed to save brightness setting: %d", ret);
        /*
         * 反映は成功している可能性があるので、そのまま値は保持する。
         */
    }
#endif

    LOG_DBG("Brightness set to %u", g_brightness);
    return 0;
}

int display_settings_save(void)
{
    if (!g_initialized) {
        display_settings_init();
    }

#if DONGLE_SCREEN_HAS_SETTINGS
    int ret = settings_save_one(DISPLAY_SETTINGS_KEY, &g_brightness, sizeof(g_brightness));
    if (ret) {
        LOG_WRN("Failed to save brightness setting: %d", ret);
        return ret;
    }
#endif

    return 0;
}
