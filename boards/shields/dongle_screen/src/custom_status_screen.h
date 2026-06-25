#pragma once

#include <lvgl.h>
#include <stdbool.h>

lv_obj_t *zmk_display_status_screen(void);

/*
 * brightness_screen / system_settings_widget から参照する
 * UI操作中フラグ。
 *
 * - brightnessスライダをドラッグ中
 * - quick actions のボタン押下中
 *
 * は true になり、画面スワイプ遷移を抑止する。
 */
extern volatile bool ui_interaction_active;
