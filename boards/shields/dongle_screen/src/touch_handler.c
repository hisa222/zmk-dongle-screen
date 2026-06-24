/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "touch_handler.h"
#include "events/swipe_gesture_event.h"

/*
 * [修正1] display_settings_is_interacting のシグネチャを変更。
 *
 * 旧: bool display_settings_is_interacting(void)
 * 新: bool display_settings_is_interacting(uint16_t raw_x, uint16_t raw_y)
 *
 * 理由:
 *   touch_input_callback (Zephyrスレッド) と LVGL処理 (専用スレッド) は非同期。
 *   タッチ DOWN から UP の間に LVGL が LV_EVENT_PRESSED を処理する保証がないため、
 *   LVGLイベント起点の slider_dragging フラグは間に合わないケースがある。
 *
 *   生タッチ座標を受け取ることで、brightness_screen.c 側がスライダー領域かどうかを
 *   スレッド競合なしに即座に判定できる。
 */
__attribute__((weak)) bool display_settings_is_interacting(uint16_t raw_x, uint16_t raw_y) {
    ARG_UNUSED(raw_x);
    ARG_UNUSED(raw_y);
    return false;
}

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <zmk/event_manager.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define TOUCH_NODE DT_NODELABEL(touch_sensor)

#if !DT_NODE_EXISTS(TOUCH_NODE)
#error "Touch sensor device tree node not found"
#endif

#define SWIPE_THRESHOLD 30

static struct touch_event_data last_event = {0};
static touch_event_callback_t registered_callback = NULL;
static bool touch_active = false;
static bool prev_touch_active = false;

static lv_indev_t *lvgl_indev = NULL;

static uint16_t current_x = 0;
static uint16_t current_y = 0;
static bool x_updated = false;
static bool y_updated = false;

static struct {
    int16_t start_x;
    int16_t start_y;
    int64_t start_time;
    bool in_progress;
} swipe_state = {0};

#define SWIPE_COOLDOWN_MS 400
static int64_t last_swipe_time = 0;

static bool swipe_already_raised = false;

#define DOUBLE_TAP_THRESHOLD 10
#define DOUBLE_TAP_INTERVAL_MS 350
static int64_t last_tap_time = 0;

/*
 * [追加] touch_handler_is_swiping()
 *
 * タッチ DOWN から UP の間 true を返す。
 * system_settings_widget.c のボタンコールバックがスワイプ中の誤発火を
 * ガードするために参照する。
 */
bool touch_handler_is_swiping(void) {
    return swipe_state.in_progress;
}

static void raise_swipe_event(enum swipe_direction direction) {
    const char *dir_name[] = {"UP", "DOWN", "LEFT", "RIGHT"};

    int64_t now = k_uptime_get();
    if ((now - last_swipe_time) < SWIPE_COOLDOWN_MS) {
        LOG_DBG("Swipe blocked - cooldown active (%lld ms remaining)",
                SWIPE_COOLDOWN_MS - (now - last_swipe_time));
        return;
    }

    /*
     * [修正2] display_settings_is_interacting に生座標を渡す。
     * brightness_screen.c 側でスライダー領域を座標から即座に判定する。
     */
    if (display_settings_is_interacting(current_x, current_y)) {
        LOG_DBG("Swipe blocked - UI interaction in progress");
        return;
    }

    last_swipe_time = now;
    LOG_INF("Raising ZMK swipe event: %s", dir_name[direction]);

    raise_zmk_swipe_gesture_event(
        (struct zmk_swipe_gesture_event){.direction = direction}
    );
}

extern void touch_handler_late_register_callback(touch_event_callback_t callback);

static void touch_input_callback(struct input_event *evt, void *user_data) {
    ARG_UNUSED(user_data);
    LOG_DBG("INPUT EVENT: type=%d code=%d value=%d", evt->type, evt->code, evt->value);

    switch (evt->code) {
        case INPUT_KEY_DOWN:
            if (evt->value == 1) {
                LOG_INF("HW GESTURE: Swipe DOWN");
                swipe_already_raised = true;
                raise_swipe_event(SWIPE_DIRECTION_DOWN);
            }
            break;

        case INPUT_KEY_UP:
            if (evt->value == 1) {
                LOG_INF("HW GESTURE: Swipe UP");
                swipe_already_raised = true;
                raise_swipe_event(SWIPE_DIRECTION_UP);
            }
            break;

        case INPUT_KEY_LEFT:
            if (evt->value == 1) {
                LOG_INF("HW GESTURE: Swipe LEFT");
                swipe_already_raised = true;
                raise_swipe_event(SWIPE_DIRECTION_LEFT);
            }
            break;

        case INPUT_KEY_RIGHT:
            if (evt->value == 1) {
                LOG_INF("HW GESTURE: Swipe RIGHT");
                swipe_already_raised = true;
                raise_swipe_event(SWIPE_DIRECTION_RIGHT);
            }
            break;

        case INPUT_ABS_X:
            current_x = (uint16_t)evt->value;
            x_updated = true;
            LOG_DBG("X: %d", current_x);
            break;

        case INPUT_ABS_Y:
            current_y = (uint16_t)evt->value;
            y_updated = true;
            LOG_DBG("Y: %d", current_y);
            break;

        case INPUT_BTN_TOUCH:
            touch_active = (evt->value != 0);
            LOG_DBG("BTN_TOUCH event: value=%d, prev_active=%d, new_active=%d",
                    evt->value, prev_touch_active, touch_active);

            if (!x_updated || !y_updated) {
                LOG_DBG("Touch event before coordinates updated, using previous values");
            }

            last_event.x = current_x;
            last_event.y = current_y;
            last_event.touched = touch_active;
            last_event.timestamp = k_uptime_get_32();

            bool touch_started = touch_active && !prev_touch_active;

            LOG_DBG("Touch state: touch_active=%d, prev=%d, started=%d",
                    touch_active, prev_touch_active, touch_started);

            if (touch_started) {
                swipe_state.start_x = current_x;
                swipe_state.start_y = current_y;
                swipe_state.start_time = k_uptime_get();
                swipe_state.in_progress = true;
                swipe_already_raised = false;

                LOG_DBG("Touch DOWN at (%d, %d)", current_x, current_y);

                /*
                 * [修正3] タッチ DOWN 時点でスライダー領域かを即座に判定し、
                 *         スワイプを事前ブロックする。
                 *
                 * 背景:
                 *   LVGL専用スレッドが LV_EVENT_PRESSED を処理する前に
                 *   Zephyrスレッドの touch_input_callback が UP を処理して
                 *   スワイプ判定に到達してしまう競合がある。
                 *   生座標で判定することでスレッド競合なしに即座にブロックできる。
                 */
                if (display_settings_is_interacting(current_x, current_y)) {
                    swipe_already_raised = true;
                    LOG_DBG("Touch DOWN on interactive area - swipe pre-blocked");
                }

                x_updated = false;
                y_updated = false;
            } else if (touch_active) {
                LOG_DBG("Dragging at (%d, %d)", current_x, current_y);
            } else {
                // Touch UP
                if (!swipe_already_raised && swipe_state.in_progress) {
                    int16_t raw_dx = current_x - swipe_state.start_x;
                    int16_t raw_dy = current_y - swipe_state.start_y;

                    // COORDINATE TRANSFORM for ROTATED_270 display
                    // Touch Y → Display X (direct), Touch X → Display Y (inverted)
                    int16_t dx = raw_dy;
                    int16_t dy = -raw_dx;

                    int16_t abs_dx = (dx < 0) ? -dx : dx;
                    int16_t abs_dy = (dy < 0) ? -dy : dy;

                    /*
                     * [修正4] スワイプ判定を単純な大小比較に変更。
                     *
                     * 旧: abs_dy > (abs_dx * 3 / 2) — 1.5倍差が必要で不感帯が生じた
                     * 新: abs_dy > abs_dx            — どちらが大きいかだけで判定
                     */
                    if (abs_dy > abs_dx && abs_dy > SWIPE_THRESHOLD) {
                        LOG_INF("SW SWIPE: %s (dy=%d)", dy > 0 ? "DOWN" : "UP", dy);
                        raise_swipe_event(dy > 0 ? SWIPE_DIRECTION_DOWN : SWIPE_DIRECTION_UP);
                    } else if (abs_dx > abs_dy && abs_dx > SWIPE_THRESHOLD) {
                        LOG_INF("SW SWIPE: %s (dx=%d)", dx > 0 ? "RIGHT" : "LEFT", dx);
                        raise_swipe_event(dx > 0 ? SWIPE_DIRECTION_RIGHT : SWIPE_DIRECTION_LEFT);
                    } else if (abs_dx <= DOUBLE_TAP_THRESHOLD && abs_dy <= DOUBLE_TAP_THRESHOLD) {
                        int64_t tap_now = k_uptime_get();
                        if ((tap_now - last_tap_time) < DOUBLE_TAP_INTERVAL_MS && last_tap_time > 0) {
                            LOG_INF("DOUBLE TAP detected");
                            last_tap_time = 0;
                            raise_swipe_event(SWIPE_DIRECTION_DOUBLE_TAP);
                        } else {
                            last_tap_time = tap_now;
                        }
                    }
                }

                swipe_state.in_progress = false;
                swipe_already_raised = false;

                x_updated = false;
                y_updated = false;
            }

            if (registered_callback) {
                registered_callback(&last_event);
            }

            prev_touch_active = touch_active;
            break;

        default:
            LOG_DBG("Unknown input event: type=%d, code=%d, value=%d",
                    evt->type, evt->code, evt->value);
            break;
    }
}

// Zephyr 3.5: INPUT_CALLBACK_DEFINE は 2引数
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(TOUCH_NODE), touch_input_callback);

// LVGL 8 input device read callback
// COORDINATE TRANSFORM:
// - Touch panel physical: 240 x 280 (portrait)
// - Display logical: 280 x 240 (landscape, ROTATED_270)
// - Touch Y (0-279) → Display X (0-279): direct mapping
// - Touch X (0-239) → Display Y (239-0): inverted
static void lvgl_input_read(lv_indev_t *indev, lv_indev_data_t *data) {
    ARG_UNUSED(indev);

    int32_t logical_x = current_y;
    int32_t logical_y = 239 - current_x;

    if (logical_x < 0) logical_x = 0;
    if (logical_x > 279) logical_x = 279;
    if (logical_y < 0) logical_y = 0;
    if (logical_y > 239) logical_y = 239;

    data->point.x = logical_x;
    data->point.y = logical_y;
    data->state = touch_active ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    static uint32_t last_log_time = 0;
    uint32_t now = k_uptime_get_32();
    if (now - last_log_time > 500 || touch_active) {
        LOG_DBG("LVGL read: raw(%d,%d) -> logical(%d,%d) state=%s",
                current_x, current_y,
                (int)data->point.x, (int)data->point.y,
                touch_active ? "PRESSED" : "RELEASED");
        last_log_time = now;
    }
}

int touch_handler_init(void) {
    const struct device *touch_dev = DEVICE_DT_GET(TOUCH_NODE);

    if (!device_is_ready(touch_dev)) {
        LOG_ERR("Touch sensor device not ready");
        return -ENODEV;
    }

    LOG_INF("Touch handler initialized: CST816S on I2C");
    LOG_INF("Touch panel size: 240x280 (Waveshare 1.69\" Round LCD)");
    LOG_INF("LVGL indev will be registered later by scanner_display.c");

    return 0;
}

int touch_handler_register_lvgl_indev(void) {
    if (lvgl_indev) {
        LOG_WRN("LVGL input device already registered");
        return 0;
    }

    // LVGL 8 API
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_input_read;
    lvgl_indev = lv_indev_drv_register(&indev_drv);
    if (!lvgl_indev) {
        LOG_ERR("Failed to register LVGL input device");
        return -ENOMEM;
    }

    LOG_INF("LVGL input device registered for touch events");

    return 0;
}

int touch_handler_register_callback(touch_event_callback_t callback) {
    if (!callback) {
        LOG_ERR("Callback is NULL!");
        return -EINVAL;
    }

    registered_callback = callback;
    LOG_INF("Touch callback registered: callback=%p", (void*)callback);

    return 0;
}

int touch_handler_get_last_event(struct touch_event_data *event) {
    if (!event) {
        return -EINVAL;
    }

    if (last_event.timestamp == 0) {
        return -ENODATA;
    }

    *event = last_event;
    return 0;
}
