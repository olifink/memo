#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "render.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    char *b;
    int len;
} AppendBuffer;

#define ABUF_INIT {NULL, 0}

static void ab_append(AppendBuffer *ab, const char *s, int len) {
    char *new_b = realloc(ab->b, ab->len + len);
    if (!new_b) return;
    memcpy(new_b + ab->len, s, len);
    ab->b = new_b;
    ab->len += len;
}

static void ab_free(AppendBuffer *ab) {
    free(ab->b);
    ab->b = NULL;
    ab->len = 0;
}

void render_init(RenderContext *ctx) {
    if (!ctx) return;
    ctx->screen_rows = 24;
    ctx->screen_cols = 80;
    ctx->row_offset = 0;
    ctx->col_offset = 0;
    ctx->filename = NULL;
    ctx->status_msg[0] = '\0';
}

void render_set_status_msg(RenderContext *ctx, const char *fmt, ...) {
    if (!ctx) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->status_msg, sizeof(ctx->status_msg), fmt, ap);
    va_end(ap);
}

static void update_viewport(RenderContext *ctx, const Buffer *buf, const CursorState *cs) {
    size_t cur_row, cur_col;
    cursor_get_line_col(buf, cs->pencil_pos, &cur_row, &cur_col);

    /* Text area occupies screen_rows - 2 rows (reserve 2 rows for status and leap banner) */
    int text_rows = ctx->screen_rows - 2;
    if (text_rows < 1) text_rows = 1;

    if (cur_row < ctx->row_offset) {
        ctx->row_offset = cur_row;
    }
    if (cur_row >= ctx->row_offset + (size_t)text_rows) {
        ctx->row_offset = cur_row - (size_t)text_rows + 1;
    }

    if (cur_col < ctx->col_offset) {
        ctx->col_offset = cur_col;
    }
    if (cur_col >= ctx->col_offset + (size_t)ctx->screen_cols) {
        ctx->col_offset = cur_col - (size_t)ctx->screen_cols + 1;
    }
}

void render_screen(RenderContext *ctx, const Buffer *buf, const CursorState *cs, const LeapState *ls) {
    if (!ctx || !buf || !cs) return;

    update_viewport(ctx, buf, cs);

    AppendBuffer ab = ABUF_INIT;

    /* Hide cursor while updating frame */
    ab_append(&ab, "\x1b[?25l", 6);
    /* Reposition to top-left (1,1) */
    ab_append(&ab, "\x1b[H", 3);

    int text_rows = ctx->screen_rows - 2;
    if (text_rows < 1) text_rows = 1;

    size_t total_buf_len = buffer_length(buf);

    /* Find buffer offset corresponding to ctx->row_offset */
    size_t line_start = 0;
    size_t line_index = 0;
    for (size_t i = 0; i < total_buf_len && line_index < ctx->row_offset; i++) {
        if (buffer_char_at(buf, i) == '\n') {
            line_index++;
            line_start = i + 1;
        }
    }

    size_t buf_pos = line_start;

    /* Render text rows */
    for (int y = 0; y < text_rows; y++) {
        if (buf_pos <= total_buf_len && (line_index + (size_t)y) >= ctx->row_offset) {
            /* Scan current line */
            size_t l_start = buf_pos;
            while (buf_pos < total_buf_len && buffer_char_at(buf, buf_pos) != '\n') {
                buf_pos++;
            }
            size_t l_end = buf_pos;
            if (buf_pos < total_buf_len && buffer_char_at(buf, buf_pos) == '\n') {
                buf_pos++; /* Advance past newline */
            }

            /* Render characters with column clipping & styling */
            size_t col = 0;
            bool highlight_active = false;

            for (size_t p = l_start; p <= l_end; p++) {
                bool is_eraser = (cs->eraser_start < cs->eraser_end &&
                                  p >= cs->eraser_start && p < cs->eraser_end);

                /* Handle trailing newline / line-end position */
                if (p == l_end) {
                    if (is_eraser && col >= ctx->col_offset &&
                        (int)(col - ctx->col_offset) < ctx->screen_cols) {
                        /* Show highlighted space for empty line or newline */
                        ab_append(&ab, "\x1b[7m \x1b[0m", 9);
                        col++;
                    }
                    break;
                }

                char c = buffer_char_at(buf, p);

                if (is_eraser && !highlight_active) {
                    ab_append(&ab, "\x1b[7m", 4);
                    highlight_active = true;
                } else if (!is_eraser && highlight_active) {
                    ab_append(&ab, "\x1b[0m", 4);
                    highlight_active = false;
                }

                if (c == '\t') {
                    int spaces = 4 - (col % 4);
                    for (int s = 0; s < spaces; s++) {
                        if (col >= ctx->col_offset && (int)(col - ctx->col_offset) < ctx->screen_cols) {
                            ab_append(&ab, " ", 1);
                        }
                        col++;
                    }
                } else {
                    if (col >= ctx->col_offset && (int)(col - ctx->col_offset) < ctx->screen_cols) {
                        ab_append(&ab, &c, 1);
                    }
                    col++;
                }
            }

            if (highlight_active) {
                ab_append(&ab, "\x1b[0m", 4);
            }
        } else {
            /* Tilde for empty lines beyond buffer */
            ab_append(&ab, "~", 1);
        }

        /* Clear to end of line and add carriage return/newline */
        ab_append(&ab, "\x1b[K\r\n", 5);
    }

    /* Render Status Bar (Row text_rows + 1) */
    ab_append(&ab, "\x1b[7m", 4); /* Inverted colors */
    char status[256];
    char rstatus[64];

    size_t cur_row, cur_col;
    cursor_get_line_col(buf, cs->pencil_pos, &cur_row, &cur_col);

    int rlen = snprintf(rstatus, sizeof(rstatus), " %zu:%zu | %zuB ",
                        cur_row + 1, cur_col + 1, total_buf_len);

    int len = snprintf(status, sizeof(status), " memo | %s%s ",
                       ctx->filename ? ctx->filename : "[No Name]",
                       buf->dirty ? " *" : "");

    if (len > ctx->screen_cols) len = ctx->screen_cols;
    ab_append(&ab, status, len);

    while (len < ctx->screen_cols) {
        if (ctx->screen_cols - len == rlen) {
            ab_append(&ab, rstatus, rlen);
            break;
        } else {
            ab_append(&ab, " ", 1);
            len++;
        }
    }
    ab_append(&ab, "\x1b[0m\r\n", 6);

    /* Render Message Bar / LEAP Banner (Row text_rows + 2) */
    ab_append(&ab, "\x1b[K", 3);
    if (ls && ls->active) {
        char leap_bar[256];
        const char *dir_str = (ls->direction == LEAP_DIR_FORWARD) ? "FWD [Right-Alt]" : "BACK [Left-Alt]";
        const char *match_info = "";
        if (ls->query_len > 0) {
            if (ls->matched) {
                match_info = ls->wrapped ? " [MATCH (Wrapped)]" : " [MATCH]";
            } else {
                match_info = " [NOT FOUND]";
            }
        }

        int lbar_len = snprintf(leap_bar, sizeof(leap_bar),
                                "\x1b[1;36mLEAP %s\x1b[0m: \"%s\"%s \x1b[90m(Release ALT: commit | Tap ALT: repeat)\x1b[0m",
                                dir_str, ls->query, match_info);
        ab_append(&ab, leap_bar, lbar_len);
    } else if (ctx->status_msg[0] != '\0') {
        int mlen = (int)strlen(ctx->status_msg);
        if (mlen > ctx->screen_cols) mlen = ctx->screen_cols;
        ab_append(&ab, ctx->status_msg, mlen);
    }

    /* Position the hardware cursor at the Pencil location */
    int screen_cursor_y = (int)(cur_row - ctx->row_offset) + 1;
    int screen_cursor_x = (int)(cur_col - ctx->col_offset) + 1;

    char cursor_pos_cmd[32];
    int c_len = snprintf(cursor_pos_cmd, sizeof(cursor_pos_cmd),
                         "\x1b[%d;%dH", screen_cursor_y, screen_cursor_x);
    ab_append(&ab, cursor_pos_cmd, c_len);

    /* Show cursor */
    ab_append(&ab, "\x1b[?25h", 6);

    /* Write frame atomically */
    ssize_t written = write(STDOUT_FILENO, ab.b, ab.len);
    (void)written;

    ab_free(&ab);
}
