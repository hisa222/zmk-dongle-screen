// touch_handler.c
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <lvgl.h>
#include "touch_handler.h"

// ---- スワイプ検出パラメーター ----
#define SWIPE_THRESHOLD   40   // px：この距離以上動いたらスワイプと判定
#define SWIPE_TIMEOUT_MS  400  // ms：この時間内に離したらスワイプ有効

// ---- 外部宣言（YADSの画面オブジェクト） ----
// YADSの既存画面変数名に合わせて変更すること
extern lv_obj_t *main_screen;
extern lv_obj_t *settings_screen;

// ---- 内部状態 ----
static int32_t touch_start_x = -1;
static int32_t touch_start_y = -1;
static int32_t touch_cur_x   = -1;
static int32_t touch_cur_y   = -1;
static bool    touching       = false;
static int64_t touch_start_ms = 0;

// ---- LVGL mutex（YADSが既に持っている場合はそちらを使う） ----
extern struct k_mutex lvgl_mutex;  // YADSの既存mutexを参照

// ---- スワイプ判定と画面遷移 ----
static void handle_swipe(int32_t dx, int32_t dy)
{
    // Prospectorは画面が90°回転しているので X/Y を入れ替えて解釈
    // （実機で確認しながら調整すること）
    int32_t horizontal = dy;   // 物理的な左右方向
    int32_t vertical   = dx;   // 物理的な上下方向

    k_mutex_lock(&lvgl_mutex, K_FOREVER);

    if (horizontal > SWIPE_THRESHOLD) {
        // 右スワイプ → メイン画面へ
        lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
    } else if (horizontal < -SWIPE_THRESHOLD) {
        // 左スワイプ → 設定画面へ
        lv_scr_load_anim(settings_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
    }
    // 上下スワイプは必要に応じて追加

    k_mutex_unlock(&lvgl_mutex);
}

// ---- Zephyr Input コールバック ----
static void touch_input_cb(struct input_event *evt)
{
    switch (evt->type) {
    case INPUT_EV_ABS:
        if (evt->code == INPUT_ABS_X) {
            touch_cur_x = evt->value;
        } else if (evt->code == INPUT_ABS_Y) {
            touch_cur_y = evt->value;
        }
        break;

    case INPUT_EV_KEY:
        if (evt->code == INPUT_BTN_TOUCH) {
            if (evt->value == 1) {
                // タッチ開始
                touching       = true;
                touch_start_x  = touch_cur_x;
                touch_start_y  = touch_cur_y;
                touch_start_ms = k_uptime_get();
            } else {
                // タッチ終了
                if (touching) {
                    int64_t elapsed = k_uptime_get() - touch_start_ms;
                    if (elapsed < SWIPE_TIMEOUT_MS &&
                        touch_start_x >= 0 && touch_start_y >= 0) {
                        int32_t dx = touch_cur_x - touch_start_x;
                        int32_t dy = touch_cur_y - touch_start_y;
                        // どちらかの軸が閾値を超えていたらスワイプ
                        if (abs(dx) > SWIPE_THRESHOLD ||
                            abs(dy) > SWIPE_THRESHOLD) {
                            handle_swipe(dx, dy);
                        }
                    }
                }
                touching = false;
            }
        }
        break;
    }
}

// NULL を渡すと全デバイスのイベントを受け取る
// 特定デバイスに絞りたい場合は DEVICE_DT_GET(DT_NODELABEL(cst816s)) を渡す
INPUT_CALLBACK_DEFINE(NULL, touch_input_cb);

void touch_handler_init(void)
{
    // 現状は INPUT_CALLBACK_DEFINE で自動登録されるため追加処理は不要
    // 将来的に初期化処理が必要になった場合はここに書く
}
