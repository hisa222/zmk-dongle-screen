/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 表示輝度設定の初期化。
 * 既存値が無い場合はデフォルト値を使う。
 */
int display_settings_init(void);

/*
 * 現在の brightness 値を返す。
 * 範囲は 0..100 (%)
 */
uint8_t display_settings_get_brightness(void);

/*
 * brightness を設定する。
 * 値は 0..100 にクランプされる。
 * 可能なら即時にバックライトへ反映する。
 */
int display_settings_set_brightness(uint8_t brightness);

/*
 * brightness を永続保存する。
 * display_settings_set_brightness() だけで保存したくない場合に使う。
 * 現状実装では set 側で保存まで行うので、明示保存 API として残すだけ。
 */
int display_settings_save(void);

#ifdef __cplusplus
}
#endif
