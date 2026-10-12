#ifndef MEMO_RENDER_H
#define MEMO_RENDER_H

#include "buffer.h"
#include "cursor.h"
#include "leap.h"

typedef struct RenderContext {
    int screen_rows;
    int screen_cols;
    size_t row_offset;
    size_t col_offset;
    const char *filename;
    char status_msg[128];
} RenderContext;

void render_init(RenderContext *ctx);
void render_screen(RenderContext *ctx, const Buffer *buf, const CursorState *cs, const LeapState *ls);
void render_set_status_msg(RenderContext *ctx, const char *fmt, ...);

#endif /* MEMO_RENDER_H */
