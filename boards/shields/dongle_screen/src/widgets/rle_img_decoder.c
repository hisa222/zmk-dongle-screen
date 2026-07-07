#include "rle_img_decoder.h"

/*
 * Custom LVGL8 image decoder for the "bongo_spheal" RLE-compressed images.
 *
 * Data layout of each image's `.data` blob:
 *   [0]                       : num_colors (N)
 *   [1 .. 1+N*4-1]            : palette, N * (R,G,B,A) bytes
 *   [1+N*4 .. data_size-1]    : RLE stream, pairs of (color_index, run_length)
 *                               run_length is 1..255, longer runs are split
 *                               into multiple consecutive pairs.
 *
 * We hook this in via LV_IMG_CF_RAW_ALPHA, which LVGL routes to registered
 * decoders. On open() we fully decode into a heap buffer as
 * LV_IMG_CF_TRUE_COLOR_ALPHA (native color bytes + 1 alpha byte per pixel),
 * which LVGL can then draw directly. The buffer is freed again on close(),
 * so only one decoded frame is ever resident in RAM at a time.
 */

static lv_res_t rle_decoder_info(lv_img_decoder_t * decoder, const void * src, lv_img_header_t * header)
{
    LV_UNUSED(decoder);

    if(lv_img_src_get_type(src) != LV_IMG_SRC_VARIABLE) return LV_RES_INV;

    const lv_img_dsc_t * img_dsc = src;
    if(img_dsc->header.cf != LV_IMG_CF_RAW_ALPHA) return LV_RES_INV;

    header->w = img_dsc->header.w;
    header->h = img_dsc->header.h;
    header->always_zero = 0;
    header->cf = LV_IMG_CF_TRUE_COLOR_ALPHA;

    return LV_RES_OK;
}

static lv_res_t rle_decoder_open(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);

    if(dsc->src_type != LV_IMG_SRC_VARIABLE) return LV_RES_INV;

    const lv_img_dsc_t * img_dsc = dsc->src;
    if(img_dsc->header.cf != LV_IMG_CF_RAW_ALPHA) return LV_RES_INV;

    const uint8_t * data = img_dsc->data;
    uint8_t num_colors = data[0];
    const uint8_t * palette = &data[1];
    const uint8_t * rle = &data[1 + (uint32_t)num_colors * 4];
    uint32_t rle_len = img_dsc->data_size - (1 + (uint32_t)num_colors * 4);

    uint32_t w = dsc->header.w;
    uint32_t h = dsc->header.h;
    uint32_t px_count = w * h;

    const uint8_t px_size = sizeof(lv_color_t) + 1; /* color bytes + 1 alpha byte */
    uint32_t buf_size = px_count * px_size;

    uint8_t * buf = lv_mem_alloc(buf_size);
    if(buf == NULL) {
        LV_LOG_ERROR("rle_img_decoder: out of memory (%d bytes)", (int)buf_size);
        return LV_RES_INV;
    }

    uint32_t px_idx = 0;
    uint32_t rle_pos = 0;
    while(rle_pos + 1 < rle_len && px_idx < px_count) {
        uint8_t color_idx = rle[rle_pos];
        uint8_t run = rle[rle_pos + 1];
        rle_pos += 2;

        if(color_idx >= num_colors) break; /* corrupt data guard */

        const uint8_t * pal_entry = &palette[(uint32_t)color_idx * 4];
        lv_color_t c = lv_color_make(pal_entry[0], pal_entry[1], pal_entry[2]);
        uint8_t alpha = pal_entry[3];

        for(uint16_t k = 0; k < run && px_idx < px_count; k++) {
            uint8_t * p = &buf[(uint32_t)px_idx * px_size];
            lv_memcpy_small(p, &c, sizeof(lv_color_t));
            p[sizeof(lv_color_t)] = alpha;
            px_idx++;
        }
    }

    dsc->img_data = buf;
    return LV_RES_OK;
}

static void rle_decoder_close(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);
    if(dsc->img_data) {
        lv_mem_free((void *)dsc->img_data);
        dsc->img_data = NULL;
    }
}

void rle_img_decoder_init(void)
{
    lv_img_decoder_t * dec = lv_img_decoder_create();
    lv_img_decoder_set_info_cb(dec, rle_decoder_info);
    lv_img_decoder_set_open_cb(dec, rle_decoder_open);
    lv_img_decoder_set_close_cb(dec, rle_decoder_close);
}
