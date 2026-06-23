/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * System Settings Widget - Bootloader / System Reset
 *
 * LV_USE_BTN が有効でないため lv_obj + LV_OBJ_FLAG_CLICKABLE で代替。
 * フォントは montserrat_20 / montserrat_40 のみ使用。
 */

#include "system_settings_widget.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <string.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ================================================================== */
/* ボタン作成ヘルパー                                                  */
/* ================================================================== */

static lv_obj_t *make_btn(lv_obj_t *parent,
                           const char *text,
                           lv_color_t bg,
                           lv_color_t bg_pressed,
                           lv_align_t align,
                           lv_coord_t x_off,
                           lv_coord_t y_off)
{
    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj) return NULL;

    lv_obj_set_size(obj, 220, 64);
    lv_obj_align(obj, align, x_off, y_off);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(obj, bg, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, bg_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_STATE_DEFAULT);

    lv_obj_t *lbl = lv_label_create(obj);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_center(lbl);

    return obj;
}

/* ================================================================== */
/* イベントハンドラ                                                    */
/* ================================================================== */

static void bootloader_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LOG_INF("Entering UF2 bootloader");
    sys_reboot(0x57); /* DFU_MAGIC_UF2_RESET */
}

static void reset_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LOG_INF("System warm reset");
    sys_reboot(SYS_REBOOT_WARM);
}

/* ================================================================== */
/* Widget init                                                         */
/* ================================================================== */

int zmk_widget_system_settings_init(struct zmk_widget_system_settings *widget,
                                    lv_obj_t *parent)
{
    LOG_INF("system_settings_widget: init");
    if (!parent) return -EINVAL;

    /* ---- コンテナ ---- */
    widget->obj = lv_obj_create(parent);
    if (!widget->obj) return -ENOMEM;

    lv_obj_set_size(widget->obj, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(widget->obj, 0, 0);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(widget->obj, lv_color_hex(0x0A0A0A), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(widget->obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(widget->obj, 0, LV_STATE_DEFAULT);

    /* ---- タイトル ---- */
    widget->title_label = lv_label_create(widget->obj);
    lv_label_set_text(widget->title_label, "Quick Actions");
    lv_obj_set_style_text_color(widget->title_label,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    /* ---- Bootloader ボタン (青) ---- */
    widget->bootloader_btn = make_btn(
        widget->obj, "Enter Bootloader",
        lv_color_hex(0x4A90E2), lv_color_hex(0x357ABD),
        LV_ALIGN_CENTER, 0, -44);
    if (!widget->bootloader_btn) { lv_obj_del(widget->obj); return -ENOMEM; }
    lv_obj_add_event_cb(widget->bootloader_btn, bootloader_cb,
                        LV_EVENT_CLICKED, NULL);

    /* ---- System Reset ボタン (赤) ---- */
    widget->reset_btn = make_btn(
        widget->obj, "System Reset",
        lv_color_hex(0xE24A4A), lv_color_hex(0xC93A3A),
        LV_ALIGN_CENTER, 0, 36);
    if (!widget->reset_btn) { lv_obj_del(widget->obj); return -ENOMEM; }
    lv_obj_add_event_cb(widget->reset_btn, reset_cb,
                        LV_EVENT_CLICKED, NULL);

    /* ---- ナビゲーションヒント ---- */
    widget->nav_hint = lv_label_create(widget->obj);
    lv_label_set_text(widget->nav_hint,
                      LV_SYMBOL_LEFT " swipe " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(widget->nav_hint,
                                lv_color_hex(0x555555), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->nav_hint,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->nav_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* 初期状態は非表示 */
    lv_obj_add_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);

    LOG_INF("system_settings_widget: init done");
    return 0;
}

void zmk_widget_system_settings_show(struct zmk_widget_system_settings *widget)
{
    if (!widget || !widget->obj) return;
    lv_obj_move_foreground(widget->obj);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);
    LOG_INF("system_settings_widget: shown");
}

void zmk_widget_system_settings_hide(struct zmk_widget_system_settings *widget)
{
    if (!widget || !widget->obj) return;
    lv_obj_add_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);
    LOG_INF("system_settings_widget: hidden");
}
