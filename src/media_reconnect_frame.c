#include "media_reconnect_frame.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
#include <jerror.h>

#include "doorfast.h"
#include "gvs_video_reassembly.h"

/* Four 16x16 bitmaps: zheng, zai, zhong, lian. */
static const uint16_t reconnect_glyphs[4][16] = {
    {0x0000, 0x7ffe, 0x0180, 0x0180, 0x0180, 0x1f80, 0x0180, 0x0180,
     0x0180, 0x6180, 0x6180, 0x6180, 0x6180, 0x6180, 0x7ffe, 0x0000},
    {0x0000, 0x7ffe, 0x0300, 0x0600, 0x0c00, 0x1800, 0x3800, 0x6ff8,
     0x0c60, 0x0c60, 0x0c60, 0x7ffe, 0x0c60, 0x0c60, 0x0ffc, 0x0000},
    {0x0000, 0x0ff0, 0x0180, 0x7ffe, 0x0180, 0x3ffc, 0x2184, 0x2184,
     0x3ffc, 0x2184, 0x2184, 0x3ffc, 0x0180, 0x7ffe, 0x0180, 0x0000},
    {0x0000, 0x0c00, 0x0c00, 0x1ffc, 0x0180, 0x3ffc, 0x0180, 0x3ffc,
     0x0180, 0x7ffe, 0x0c00, 0x1c00, 0x3000, 0x67fe, 0x3ffe, 0x0000},
};

struct df_jpeg_error {
    struct jpeg_error_mgr base;
    jmp_buf jump;
};

struct df_jpeg_state {
    struct jpeg_compress_struct compressor;
    struct jpeg_decompress_struct decoder;
    struct df_jpeg_error error;
    struct jpeg_destination_mgr destination;
    uint8_t *row;
    uint8_t *output;
};

/* A compact ASCII font for the deliberately small stale-image notice. */
static const uint8_t df_notice_font[26][7] = {
    ['C'-'A'] = {14,17,16,16,16,17,14},
    ['E'-'A'] = {31,16,16,30,16,16,31},
    ['F'-'A'] = {31,16,16,30,16,16,16},
    ['G'-'A'] = {14,17,16,23,17,17,14},
    ['I'-'A'] = {31,4,4,4,4,4,31},
    ['M'-'A'] = {17,27,21,21,17,17,17},
    ['N'-'A'] = {17,25,25,21,19,19,17},
    ['O'-'A'] = {14,17,17,17,17,17,14},
    ['R'-'A'] = {30,17,17,30,20,18,17},
    ['T'-'A'] = {31,4,4,4,4,4,4},
    ['Z'-'A'] = {31,1,2,4,8,16,31},
};

static void df_notice_row(uint8_t *row, unsigned width, unsigned y) {
    static const char text[] = "RECONNECTING - FROZEN FRAME";
    unsigned scale = width >= 400U ? 2U : 1U;
    unsigned start = 8U, top = 8U;
    if (y < top || y >= top + 11U * scale) return;
    for (unsigned x = start; x < width &&
            x < start + (sizeof(text) - 1U) * 6U * scale + 4U; x++) {
        bool bright = false;
        unsigned col = x - start;
        if (y >= top + 2U * scale && y < top + 9U * scale &&
            col >= 2U * scale) {
            col = col / scale - 2U;
            unsigned index = col / 6U, bit = col % 6U;
            if (index < sizeof(text) - 1U && bit < 5U) {
                char c = text[index];
                unsigned gy = (y - top) / scale - 2U;
                if (c >= 'A' && c <= 'Z')
                    bright = (df_notice_font[c-'A'][gy] & (16U >> bit)) != 0U;
                else if (c == '-') bright = gy == 3U;
            }
        }
        memset(row + (size_t)x * 3U, bright ? 242 : 20, 3U);
    }
}

static void df_jpeg_error_exit(j_common_ptr common)
{
    struct df_jpeg_error *error = (struct df_jpeg_error *)common->err;
    longjmp(error->jump, 1);
}

static void df_jpeg_init_destination(j_compress_ptr compressor)
{
    struct df_jpeg_state *state = (struct df_jpeg_state *)compressor->client_data;
    state->destination.next_output_byte = state->output;
    state->destination.free_in_buffer = DF_GVS_VIDEO_MAX_FRAME;
}

static boolean df_jpeg_buffer_full(j_compress_ptr compressor)
{
    ERREXIT(compressor, JERR_BUFFER_SIZE);
    return FALSE;
}

static void df_jpeg_finish_destination(j_compress_ptr compressor)
{
    (void)compressor;
}

static void df_render_row(uint8_t *row, unsigned int width, unsigned int y,
                          unsigned int scale, unsigned int gap,
                          unsigned int start_x, unsigned int start_y)
{
    for (unsigned int x = 0; x < width; x++) {
        unsigned int glyph_column;
        unsigned int glyph_index;
        unsigned int bit_column;
        unsigned int bit_row;
        size_t pixel = (size_t)x * 3U;

        row[pixel] = 20U;
        row[pixel + 1U] = 28U;
        row[pixel + 2U] = 35U;
        if (x < start_x || y < start_y ||
            y >= start_y + 16U * scale) continue;
        glyph_column = (x - start_x) / scale;
        glyph_index = glyph_column / (16U + gap);
        bit_column = glyph_column % (16U + gap);
        if (glyph_index >= 4U || bit_column >= 16U) continue;
        bit_row = (y - start_y) / scale;
        if ((reconnect_glyphs[glyph_index][bit_row] &
             (uint16_t)(0x8000U >> bit_column)) == 0U) continue;
        row[pixel] = 242U;
        row[pixel + 1U] = 244U;
        row[pixel + 2U] = 235U;
    }
}

int df_media_reconnect_frame_create(uint16_t width, uint16_t height,
                                    struct df_media_reconnect_frame *out)
{
    struct df_jpeg_state *state;
    unsigned int gap;
    unsigned int scale;
    unsigned int text_width;
    unsigned int start_x;
    unsigned int start_y;
    size_t row_length;
    volatile int result = DF_ERR_IO;

    if (out == NULL) return DF_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    if (width < 64U || height < 64U ||
        (size_t)width * (size_t)height > 4096U * 2160U) return DF_ERR_INVALID;

    row_length = (size_t)width * 3U;
    if (row_length / 3U != (size_t)width) return DF_ERR_INVALID;
    state = calloc(1, sizeof(*state));
    if (state == NULL) return DF_ERR_IO;
    state->row = malloc(row_length);
    state->output = malloc(DF_GVS_VIDEO_MAX_FRAME);
    if (state->row == NULL || state->output == NULL) goto cleanup;

    state->compressor.err = jpeg_std_error(&state->error.base);
    state->error.base.error_exit = df_jpeg_error_exit;
    if (setjmp(state->error.jump) != 0) goto cleanup;
    jpeg_create_compress(&state->compressor);
    state->compressor.client_data = state;
    state->destination.init_destination = df_jpeg_init_destination;
    state->destination.empty_output_buffer = df_jpeg_buffer_full;
    state->destination.term_destination = df_jpeg_finish_destination;
    state->compressor.dest = &state->destination;
    state->compressor.image_width = width;
    state->compressor.image_height = height;
    state->compressor.input_components = 3;
    state->compressor.in_color_space = JCS_RGB;
    jpeg_set_defaults(&state->compressor);
    jpeg_set_quality(&state->compressor, 85, TRUE);

    gap = width < 70U ? 0U : 2U;
    scale = width / (64U + 3U * gap);
    if (scale > height / 16U) scale = height / 16U;
    if (scale > 8U) scale = 8U;
    text_width = (64U + 3U * gap) * scale;
    start_x = ((unsigned int)width - text_width) / 2U;
    start_y = ((unsigned int)height - 16U * scale) / 2U;

    jpeg_start_compress(&state->compressor, TRUE);
    while (state->compressor.next_scanline < state->compressor.image_height) {
        JSAMPROW row = state->row;
        df_render_row(state->row, width, state->compressor.next_scanline,
                      scale, gap, start_x, start_y);
        jpeg_write_scanlines(&state->compressor, &row, 1);
    }
    jpeg_finish_compress(&state->compressor);
    out->length = DF_GVS_VIDEO_MAX_FRAME -
                  state->destination.free_in_buffer;
    if (out->length == 0U || out->length > DF_GVS_VIDEO_MAX_FRAME) {
        out->length = 0U;
        goto cleanup;
    }
    out->data = state->output;
    state->output = NULL;
    out->width = width;
    out->height = height;
    result = DF_OK;

cleanup:
    if (state->compressor.mem != NULL) jpeg_destroy_compress(&state->compressor);
    free(state->row);
    free(state->output);
    free(state);
    if (result != DF_OK) memset(out, 0, sizeof(*out));
    return result;
}

void df_media_reconnect_frame_destroy(struct df_media_reconnect_frame *frame)
{
    if (frame == NULL) return;
    free(frame->data);
    memset(frame, 0, sizeof(*frame));
}

static void df_jpeg_ignore_message(j_common_ptr common) {
    (void)common;
}

static int df_media_freeze_quality(const uint8_t *jpeg, size_t length,
    uint16_t width, uint16_t height, int quality, bool *too_large,
    struct df_media_reconnect_frame *out) {
    struct df_jpeg_state *state;
    volatile int result = DF_ERR_IO;
    *too_large = false;
    if (out == NULL) return DF_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    if (jpeg == NULL || length == 0U || length > DF_GVS_VIDEO_MAX_FRAME ||
        width == 0U || height == 0U ||
        (size_t)width * height > 4096U * 2160U) return DF_ERR_INVALID;
    state = calloc(1U, sizeof(*state));
    if (state == NULL) return DF_ERR_IO;
    state->row = malloc((size_t)width * 3U);
    state->output = malloc(DF_GVS_VIDEO_MAX_FRAME);
    if (state->row == NULL || state->output == NULL) goto cleanup;
    state->decoder.err = jpeg_std_error(&state->error.base);
    state->compressor.err = &state->error.base;
    state->error.base.error_exit = df_jpeg_error_exit;
    state->error.base.output_message = df_jpeg_ignore_message;
    if (setjmp(state->error.jump) != 0) goto cleanup;
    jpeg_create_decompress(&state->decoder);
    jpeg_mem_src(&state->decoder, jpeg, (unsigned long)length);
    if (jpeg_read_header(&state->decoder, TRUE) != JPEG_HEADER_OK ||
        state->decoder.image_width != width ||
        state->decoder.image_height != height) goto cleanup;
    state->decoder.out_color_space = JCS_RGB;
    jpeg_start_decompress(&state->decoder);
    jpeg_create_compress(&state->compressor);
    state->compressor.client_data = state;
    state->destination.init_destination = df_jpeg_init_destination;
    state->destination.empty_output_buffer = df_jpeg_buffer_full;
    state->destination.term_destination = df_jpeg_finish_destination;
    state->compressor.dest = &state->destination;
    state->compressor.image_width = width;
    state->compressor.image_height = height;
    state->compressor.input_components = 3;
    state->compressor.in_color_space = JCS_RGB;
    jpeg_set_defaults(&state->compressor);
    jpeg_set_quality(&state->compressor, quality, TRUE);
    jpeg_start_compress(&state->compressor, TRUE);
    while (state->decoder.output_scanline < height) {
        unsigned y = state->decoder.output_scanline;
        JSAMPROW row = state->row;
        jpeg_read_scanlines(&state->decoder, &row, 1U);
        df_notice_row(state->row, width, y);
        jpeg_write_scanlines(&state->compressor, &row, 1U);
    }
    jpeg_finish_decompress(&state->decoder);
    jpeg_finish_compress(&state->compressor);
    out->length = DF_GVS_VIDEO_MAX_FRAME - state->destination.free_in_buffer;
    out->data = state->output;
    state->output = NULL;
    out->width = width;
    out->height = height;
    result = DF_OK;
cleanup:
    *too_large = state->error.base.msg_code == JERR_BUFFER_SIZE;
    if (state->decoder.mem != NULL) jpeg_destroy_decompress(&state->decoder);
    if (state->compressor.mem != NULL) jpeg_destroy_compress(&state->compressor);
    free(state->row);
    free(state->output);
    free(state);
    return result;
}

int df_media_reconnect_frame_freeze(const uint8_t *jpeg, size_t length,
    uint16_t width, uint16_t height, struct df_media_reconnect_frame *out) {
    static const int qualities[] = {85, 60, 35, 10, 5};
    for (size_t i = 0U; i < sizeof(qualities) / sizeof(qualities[0]); i++) {
        bool too_large = false;
        int result = df_media_freeze_quality(jpeg, length, width, height,
            qualities[i], &too_large, out);
        if (result == DF_OK || !too_large) return result;
    }
    return DF_ERR_IO;
}
