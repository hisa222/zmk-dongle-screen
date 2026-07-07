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
 * This version decodes ONE SCANLINE AT A TIME via the read_line callback,
 * instead of allocating a full-frame buffer. RAM usage per open image is:
 *   - a small per-row lookup table (h * 6 bytes, e.g. 140 rows = 840 B)
 *   - no full pixel buffer at all (0 B, vs. ~113 KB for the old approach)
 * This avoids exhausting LVGL's memory pool (LV_MEM_SIZE) on constrained
 * peripheral boards.
 */

typedef struct {
    const uint8_t *palette;   /* N * (R,G,B,A) */
    const uint8_t *rle;       /* RLE stream start */
    uint32_t rle_len;
    uint16_t w;
    uint16_t h;
    uint8_t num_colors;
    uint32_t *row_token_offset; /* byte offset into rle[] where each row begins */
    uint16_t *row_skip;         /* pixels already consumed of that token by earlier rows */
} rle_decoder_state_t;

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
    uint16_t w = dsc->header.w;
    uint16_t h = dsc->header.h;

    if(rle_len < 2 || w == 0 || h == 0) return LV_RES_INV;

    rle_decoder_state_t * state = lv_mem_alloc(sizeof(rle_decoder_state_t));
    if(state == NULL) return LV_RES_INV;
    lv_memset_00(state, sizeof(rle_decoder_state_t));

    state->palette = palette;
    state->rle = rle;
    state->rle_len = rle_len;
    state->w = w;
    state->h = h;
    state->num_colors = num_colors;

    state->row_token_offset = lv_mem_alloc((uint32_t)h * sizeof(uint32_t));
    state->row_skip = lv_mem_alloc((uint32_t)h * sizeof(uint16_t));
    if(state->row_token_offset == NULL || state->row_skip == NULL) {
        if(state->row_token_offset) lv_mem_free(state->row_token_offset);
        if(state->row_skip) lv_mem_free(state->row_skip);
        lv_mem_free(state);
        return LV_RES_INV;
    }

    /* Precompute, for every row, which token it starts in and how many
     * pixels of that token were already used up by earlier rows. */
    uint32_t pos = 0;
    uint16_t token_run = rle[1];
    uint16_t used = 0;

    for(uint16_t row = 0; row < h; row++) {
        state->row_token_offset[row] = pos;
        state->row_skip[row] = used;

        uint16_t need = w;
        while(need > 0) {
            uint16_t avail = token_run - used;
            if(avail == 0) { /* malformed data guard: stop early */
                need = 0;
                break;
            }
            uint16_t take = (need < avail) ? need : avail;
            used = (uint16_t)(used + take);
            need = (uint16_t)(need - take);
            if(used == token_run) {
                pos += 2;
                used = 0;
                if(pos + 1 < rle_len) {
                    token_run = rle[pos + 1];
                } else {
                    token_run = 0; /* end of data reached */
                }
            }
        }
    }

    dsc->user_data = state;
    dsc->img_data = NULL; /* signal LVGL to use read_line_cb */
    return LV_RES_OK;
}

static lv_res_t rle_decoder_read_line(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc,
                                       lv_coord_t x, lv_coord_t y, lv_coord_t len, uint8_t * buf)
{
    LV_UNUSED(decoder);
    rle_decoder_state_t * state = dsc->user_data;
    if(state == NULL || y < 0 || y >= state->h) return LV_RES_INV;

    const uint8_t * rle = state->rle;
    uint32_t rle_len = state->rle_len;

    uint32_t pos = state->row_token_offset[y];
    uint16_t used = state->row_skip[y];
    uint16_t token_run = (pos + 1 < rle_len) ? rle[pos + 1] : 0;

    /* Skip forward `x` pixels within the row without writing anything. */
    uint16_t skip_remaining = (uint16_t)x;
    while(skip_remaining > 0 && token_run > 0) {
        uint16_t avail = token_run - used;
        uint16_t take = (skip_remaining < avail) ? skip_remaining : avail;
        used = (uint16_t)(used + take);
        skip_remaining = (uint16_t)(skip_remaining - take);
        if(used == token_run) {
            pos += 2;
            used = 0;
            token_run = (pos + 1 < rle_len) ? rle[pos + 1] : 0;
        }
    }

    /* Emit `len` pixels. */
    uint8_t * out = buf;
    uint16_t remaining = (uint16_t)len;
    while(remaining > 0 && token_run > 0) {
        uint8_t color_idx = rle[pos];
        const uint8_t * pal = &state->palette[(uint32_t)color_idx * 4];
        lv_color_t c = lv_color_make(pal[0], pal[1], pal[2]);
        uint8_t alpha = pal[3];

        uint16_t avail = token_run - used;
        uint16_t take = (remaining < avail) ? remaining : avail;

        for(uint16_t k = 0; k < take; k++) {
            lv_memcpy_small(out, &c, sizeof(lv_color_t));
            out[sizeof(lv_color_t)] = alpha;
            out += sizeof(lv_color_t) + 1;
        }

        used = (uint16_t)(used + take);
        remaining = (uint16_t)(remaining - take);
        if(used == token_run) {
            pos += 2;
            used = 0;
            token_run = (pos + 1 < rle_len) ? rle[pos + 1] : 0;
        }
    }

    return LV_RES_OK;
}

static void rle_decoder_close(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);
    rle_decoder_state_t * state = dsc->user_data;
    if(state) {
        if(state->row_token_offset) lv_mem_free(state->row_token_offset);
        if(state->row_skip) lv_mem_free(state->row_skip);
        lv_mem_free(state);
        dsc->user_data = NULL;
    }
}

void rle_img_decoder_init(void)
{
    lv_img_decoder_t * dec = lv_img_decoder_create();
    lv_img_decoder_set_info_cb(dec, rle_decoder_info);
    lv_img_decoder_set_open_cb(dec, rle_decoder_open);
    lv_img_decoder_set_read_line_cb(dec, rle_decoder_read_line);
    lv_img_decoder_set_close_cb(dec, rle_decoder_close);
}
