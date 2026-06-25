/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * System Settings Widget
 *
 * Prospector 寄せのための方針:
 * - ボタン押下中は ui_interaction_active=true
 * - RELEASED / PRESS_LOST で false に戻す
 * - 実行本体は CLICKED のみ
 * - touch_handler_is_swiping() が true の間はボタン動作を無視
 *
 * これにより、Quick Actions 画面からスワイプで別画面へ移動しようとした際の
 * Bootloader / Reset ボタン誤作動を抑止する。
 */

#include "system_settings_widget.h"
#include "../touch_handler.h"
#include "../custom_status_screen.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <errno.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ================================================================== */
/* Button helper                                                      */
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
    if (!obj) {
        return NULL;
    }

    lv_obj_set_size(obj, 220, 60);
    lv_obj_align(obj, align, x_off, y_off);

    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * PRESS_LOCK を付けない:
     * スワイプで指がボタン外へ出たときに PRESS_LOST に落ちやすくする
     */
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);

    lv_obj_set_style_bg_color(obj, bg, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, bg_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_STATE_DEFAULT);

    lv_obj_t *lbl = lv_label_create(obj);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(lbl);

    return obj;
}

/* ================================================================== */
/* Interaction state callbacks                                        */
/* ================================================================== */

static void ui_press_start_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    ui_interaction_active = true;
}

static void ui_press_end_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    ui_interaction_active = false;
}

/* ================================================================== */
/* Action callbacks                                                   */
/* ================================================================== */

static void bootloader_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) {
        return;
    }

    /*
     * スワイプ中の誤発火防止
     */
    if (touch_handler_is_swiping()) {
        LOG_DBG("Bootloader button ignored - swipe in progress");
        return;
    }

    ui_interaction_active = false;
    LOG_INF("Enter UF2 bootloader");
    sys_reboot(0x57);
}

static void reset_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) {
        return;
    }

    if (touch_handler_is_swiping()) {
        LOG_DBG("Reset button ignored - swipe in progress");
        return;
    }

    ui_interaction_active = false;
    LOG_INF("System warm reset");
    sys_reboot(SYS_REBOOT_WARM);
}

/* ================================================================== */
/* Widget init                                                        */
/* ================================================================== */

int zmk_widget_system_settings_init(struct zmk_widget_system_settings *widget,
                                    lv_obj_t *parent)
{
    if (!widget || !parent) {
        return -EINVAL;
    }

    widget->obj = parent;

    widget->title_label = lv_label_create(parent);
    lv_label_set_text(widget->title_label, "Quick Actions");
    lv_obj_set_style_text_color(widget->title_label,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    /*
     * ボタン間隔は広めに取る
     */
    widget->bootloader_btn = make_btn(
        parent, "Enter Bootloader",
        lv_color_hex(0x4A90E2), lv_color_hex(0x357ABD),
        LV_ALIGN_CENTER, 0, -52);
    if (!widget->bootloader_btn) {
        return -ENOMEM;
    }

    widget->reset_btn = make_btn(
        parent, "System Reset",
        lv_color_hex(0xE24A4A), lv_color_hex(0xC93A3A),
        LV_ALIGN_CENTER, 0, 52);
    if (!widget->reset_btn) {
        return -ENOMEM;
    }

    /*
     * 押下開始/終了は interaction 管理専用
     */
    lv_obj_add_event_cb(widget->bootloader_btn, ui_press_start_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(widget->bootloader_btn, ui_press_end_cb,   LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(widget->bootloader_btn, ui_press_end_cb,   LV_EVENT_PRESS_LOST, NULL);

    lv_obj_add_event_cb(widget->reset_btn, ui_press_start_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(widget->reset_btn, ui_press_end_cb,   LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(widget->reset_btn, ui_press_end_cb,   LV_EVENT_PRESS_LOST, NULL);

    /*
     * 実行本体は CLICKED のみ
     */
    lv_obj_add_event_cb(widget->bootloader_btn, bootloader_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(widget->reset_btn, reset_cb, LV_EVENT_CLICKED, NULL);

    widget->nav_hint = lv_label_create(parent);
    lv_label_set_text(widget->nav_hint, "< swipe >");
    lv_obj_set_style_text_color(widget->nav_hint,
                                lv_color_hex(0x444444), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->nav_hint,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->nav_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    return 0;
}

void zmk_widget_system_settings_show(struct zmk_widget_system_settings *widget)
{
    ARG_UNUSED(widget);
    ui_interaction_active = false;
}

void zmk_widget_system_settings_hide(struct zmk_widget_system_settings *widget)
{
    ui_interaction_active = false;

    if (!widget) {
        return;
    }

    /*
     * 実体削除は親 screen の lv_obj_clean() 側で行う。
     * ここでは dangling pointer を残さないために NULL 化だけする。
     */
    widget->title_label = NULL;
    widget->bootloader_btn = NULL;
    widget->reset_btn = NULL;
    widget->nav_hint = NULL;
    widget->obj = NULL;
}
