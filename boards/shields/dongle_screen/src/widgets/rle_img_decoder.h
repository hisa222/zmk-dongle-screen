#ifndef RLE_IMG_DECODER_H
#define RLE_IMG_DECODER_H

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

/**
 * Registers the custom RLE image decoder with LVGL.
 * Call this once during init, before any bongo_spheal_* image is displayed
 * (e.g. near the top of your display/widget init function).
 */
void rle_img_decoder_init(void);

#endif /* RLE_IMG_DECODER_H */
