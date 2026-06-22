/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * System Settings Widget for zmk-dongle-screen
 *
 * Ported from prospector-zmk-module / system_settings_widget.c
 * Changes from original:
 *   - Scanner channel selector removed (dongle-screen has no channel concept)
 *   - Brightness selector added (uses display_settings + set_screen_brightness)
 */

#include "system_settings_widget.h"
#include "display_settings.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <string.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ------------------------------------------------------------------ */
/* Brightness step size per button press                               */
/* ------------------------------------------------------------------ */
#define BRIGHTNESS_STEP 10
#define BRIGHTNESS_MIN   1
#define BRIGHTNESS_MAX 100

/*
 * set_screen_brightness() is defined in brightness.c (non-static).
 * We call it to apply the new level immediately, then persist via
 * display_settings_set_manual_brightness() + display_settings_save_if_dirty().
 */
extern void set_screen_brightness(uint8_t value, bool ambient);

/* ------------------------------------------------------------------ */
/* Forward declaration                                                 */
/* ------------------------------------------------------------------ */
static void update_brightness_value_display(struct zmk_widget_system_settings *widget);

/* ================================================================== */
/* Button event handlers                                               */
/* ================================================================== */

static void bootloader_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED) {
        LOG_INF("Bootloader button activated - entering UF2 bootloader");
        /* 0x57 = DFU_MAGIC_UF2_RESET for Adafruit nRF52 bootloader */
        sys_reboot(0x57);
    }
}

static void reset_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED) {
        LOG_INF("Reset button activated - performing warm reboot");
        sys_reboot(SYS_REBOOT_WARM);
    }
}

/* ================================================================== */
/* Brightness button event handlers                                    */
/* ================================================================== */

static void brightness_left_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED) {
        uint8_t level = display_settings_get_manual_brightness();
        if (level > BRIGHTNESS_MIN + BRIGHTNESS_STEP) {
            level -= BRIGHTNESS_STEP;
        } else {
            level = BRIGHTNESS_MIN;
        }

        display_settings_set_manual_brightness(level);
        display_settings_save_if_dirty();
        set_screen_brightness(level, false);

        struct zmk_widget_system_settings *widget = lv_event_get_user_data(e);
        update_brightness_value_display(widget);

        LOG_INF("Brightness decreased to %d", level);
    }
}

static void brightness_right_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED) {
        uint8_t level = display_settings_get_manual_brightness();
        if (level + BRIGHTNESS_STEP <= BRIGHTNESS_MAX) {
            level += BRIGHTNESS_STEP;
        } else {
            level = BRIGHTNESS_MAX;
        }

        display_settings_set_manual_brightness(level);
        display_settings_save_if_dirty();
        set_screen_brightness(level, false);

        struct zmk_widget_system_settings *widget = lv_event_get_user_data(e);
        update_brightness_value_display(widget);

        LOG_INF("Brightness increased to %d", level);
    }
}

static void update_brightness_value_display(struct zmk_widget_system_settings *widget) {
    if (!widget || !widget->brightness_value) {
        return;
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", display_settings_get_manual_brightness());
    lv_label_set_text(widget->brightness_value, buf);
}

/* ================================================================== */
/* Helper: create a styled LVGL button                                 */
/* ================================================================== */

static lv_obj_t *create_styled_button(lv_obj_t *parent, const char *text,
                                       lv_color_t bg_color, lv_color_t bg_color_pressed,
                                       int x_offset, int y_offset) {
    lv_obj_t *btn = lv_btn_create(parent);
    if (!btn) {
        LOG_ERR("Failed to create button");
        return NULL;
    }

    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, x_offset, y_offset);

    /* Normal state */
    lv_obj_set_style_bg_color(btn, bg_color, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_lighten(bg_color, 60), LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, LV_OPA_50, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 8, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 10, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(btn, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_30, LV_STATE_DEFAULT);

    /* Pressed state */
    lv_obj_set_style_bg_color(btn, bg_color_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 5, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_50, LV_STATE_PRESSED);

    /* Label */
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_STATE_DEFAULT);
    lv_obj_center(label);

    return btn;
}

/* ================================================================== */
/* Widget initialisation                                               */
/* ================================================================== */

int zmk_widget_system_settings_init(struct zmk_widget_system_settings *widget,
                                    lv_obj_t *parent) {
    LOG_INF("System settings widget init START");

    if (!parent) {
        LOG_ERR("Parent is NULL");
        return -EINVAL;
    }

    /* Ensure brightness is loaded from NVS before showing the widget */
    display_settings_init();

    widget->parent = parent;

    /* ---- Full-screen container ---- */
    widget->obj = lv_obj_create(parent);
    if (!widget->obj) {
        LOG_ERR("Failed to create container");
        return -ENOMEM;
    }

    lv_obj_set_size(widget->obj, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(widget->obj, 0, 0);
    lv_obj_set_style_bg_color(widget->obj, lv_color_hex(0x0A0A0A), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(widget->obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(widget->obj, 0, LV_STATE_DEFAULT);

    /* ---- Title ---- */
    widget->title_label = lv_label_create(widget->obj);
    lv_label_set_text(widget->title_label, "Quick Actions");
    lv_obj_set_style_text_color(widget->title_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 15);

    /* ---- Bootloader button (blue) ---- */
    widget->bootloader_btn = create_styled_button(
        widget->obj,
        "Enter Bootloader",
        lv_color_hex(0x4A90E2),  /* sky blue   */
        lv_color_hex(0x357ABD),  /* darker blue */
        0, -35
    );
    if (!widget->bootloader_btn) {
        LOG_ERR("Failed to create bootloader button");
        lv_obj_del(widget->obj);
        return -ENOMEM;
    }
    lv_obj_add_event_cb(widget->bootloader_btn, bootloader_btn_event_cb,
                        LV_EVENT_ALL, NULL);

    /* ---- Reset button (red) ---- */
    widget->reset_btn = create_styled_button(
        widget->obj,
        "System Reset",
        lv_color_hex(0xE24A4A),  /* soft red    */
        lv_color_hex(0xC93A3A),  /* darker red  */
        0, 35
    );
    if (!widget->reset_btn) {
        LOG_ERR("Failed to create reset button");
        lv_obj_del(widget->obj);
        return -ENOMEM;
    }
    lv_obj_add_event_cb(widget->reset_btn, reset_btn_event_cb,
                        LV_EVENT_ALL, NULL);

    /* ================================================================
     * Brightness selector
     * Layout (bottom area):
     *   [<]  [  50%  ]  [>]
     *   Brightness
     * ================================================================ */

    /* "Brightness:" label */
    widget->brightness_label = lv_label_create(widget->obj);
    lv_label_set_text(widget->brightness_label, "Brightness:");
    lv_obj_set_style_text_color(widget->brightness_label,
                                lv_color_hex(0xAAAAAA), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->brightness_label,
                               &lv_font_montserrat_16, LV_STATE_DEFAULT);
    lv_obj_align(widget->brightness_label, LV_ALIGN_BOTTOM_MID, -55, -50);

    /* Value display */
    widget->brightness_value = lv_label_create(widget->obj);
    lv_obj_set_style_text_color(widget->brightness_value,
                                lv_color_hex(0x4A90E2), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->brightness_value,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_set_width(widget->brightness_value, 60);
    lv_obj_set_style_text_align(widget->brightness_value,
                                LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_obj_align(widget->brightness_value, LV_ALIGN_BOTTOM_MID, 20, -48);
    update_brightness_value_display(widget);  /* set initial text */

    /* Left ( - ) button */
    widget->brightness_left_btn = lv_btn_create(widget->obj);
    lv_obj_set_size(widget->brightness_left_btn, 40, 32);
    lv_obj_align(widget->brightness_left_btn, LV_ALIGN_BOTTOM_MID, -20, -45);
    lv_obj_set_style_bg_color(widget->brightness_left_btn,
                              lv_color_hex(0x333333), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(widget->brightness_left_btn,
                              lv_color_hex(0x555555), LV_STATE_PRESSED);
    lv_obj_set_style_radius(widget->brightness_left_btn, 6, LV_STATE_DEFAULT);

    lv_obj_t *left_label = lv_label_create(widget->brightness_left_btn);
    lv_label_set_text(left_label, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(left_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_center(left_label);

    lv_obj_add_event_cb(widget->brightness_left_btn,
                        brightness_left_btn_event_cb, LV_EVENT_ALL, widget);

    /* Right ( + ) button */
    widget->brightness_right_btn = lv_btn_create(widget->obj);
    lv_obj_set_size(widget->brightness_right_btn, 40, 32);
    lv_obj_align(widget->brightness_right_btn, LV_ALIGN_BOTTOM_MID, 70, -45);
    lv_obj_set_style_bg_color(widget->brightness_right_btn,
                              lv_color_hex(0x333333), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(widget->brightness_right_btn,
                              lv_color_hex(0x555555), LV_STATE_PRESSED);
    lv_obj_set_style_radius(widget->brightness_right_btn, 6, LV_STATE_DEFAULT);

    lv_obj_t *right_label = lv_label_create(widget->brightness_right_btn);
    lv_label_set_text(right_label, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(right_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_center(right_label);

    lv_obj_add_event_cb(widget->brightness_right_btn,
                        brightness_right_btn_event_cb, LV_EVENT_ALL, widget);

    /* Start hidden */
    lv_obj_add_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);

    LOG_INF("System settings widget initialized");
    return 0;
}

/* ================================================================== */
/* Dynamic allocation helpers                                          */
/* ================================================================== */

struct zmk_widget_system_settings *
zmk_widget_system_settings_create(lv_obj_t *parent) {
    if (!parent) {
        LOG_ERR("Cannot create widget: parent is NULL");
        return NULL;
    }

    struct zmk_widget_system_settings *widget =
        (struct zmk_widget_system_settings *)
            lv_malloc(sizeof(struct zmk_widget_system_settings));
    if (!widget) {
        LOG_ERR("Failed to allocate memory for system_settings_widget");
        return NULL;
    }

    memset(widget, 0, sizeof(struct zmk_widget_system_settings));

    int ret = zmk_widget_system_settings_init(widget, parent);
    if (ret != 0) {
        LOG_ERR("Widget initialization failed, freeing memory");
        lv_free(widget);
        return NULL;
    }

    return widget;
}

void zmk_widget_system_settings_destroy(struct zmk_widget_system_settings *widget) {
    if (!widget) {
        return;
    }

    if (widget->obj) {
        lv_obj_del(widget->obj);
        widget->obj = NULL;
    }

    widget->title_label        = NULL;
    widget->bootloader_btn     = NULL;
    widget->bootloader_label   = NULL;
    widget->reset_btn          = NULL;
    widget->reset_label        = NULL;
    widget->brightness_label   = NULL;
    widget->brightness_value   = NULL;
    widget->brightness_left_btn  = NULL;
    widget->brightness_right_btn = NULL;

    lv_free(widget);
}

/* ================================================================== */
/* Show / hide                                                          */
/* ================================================================== */

void zmk_widget_system_settings_show(struct zmk_widget_system_settings *widget) {
    if (!widget || !widget->obj) {
        LOG_ERR("Cannot show - widget or obj is NULL");
        return;
    }

    /* Sync displayed value with current NVS value each time we show */
    update_brightness_value_display(widget);

    lv_obj_move_foreground(widget->obj);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);
    LOG_INF("System settings screen shown");
}

void zmk_widget_system_settings_hide(struct zmk_widget_system_settings *widget) {
    if (!widget || !widget->obj) {
        LOG_WRN("Cannot hide - widget or obj is NULL");
        return;
    }

    lv_obj_add_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);
    LOG_INF("System settings screen hidden");
}
