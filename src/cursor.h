#ifndef MEMO_CURSOR_H
#define MEMO_CURSOR_H

#include "buffer.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct CursorState {
    size_t pencil_pos;         /* Insertion point byte offset */
    size_t eraser_start;       /* Eraser span start byte offset */
    size_t eraser_end;         /* Eraser span end byte offset */
    bool has_span_selection;   /* True if user engaged span selection */
    size_t leap_origin;        /* Origin offset before current leap series */
    int preferred_col;         /* Preferred visual column during vertical movement */
} CursorState;

void cursor_init(CursorState *cs);

/* Fundamental Cursor Rule helpers */
void cursor_after_insert(CursorState *cs, size_t inserted_len);
void cursor_after_erase(CursorState *cs, const Buffer *buf);
void cursor_coalesce_at(CursorState *cs, size_t start, size_t end);
void cursor_set_span(CursorState *cs, size_t start, size_t end);
void cursor_clear_selection(CursorState *cs);

/* Local arrow navigation */
void cursor_move_left(CursorState *cs, const Buffer *buf);
void cursor_move_right(CursorState *cs, const Buffer *buf);
void cursor_move_up(CursorState *cs, const Buffer *buf);
void cursor_move_down(CursorState *cs, const Buffer *buf);

/* Document coordinate helpers */
void cursor_get_line_col(const Buffer *buf, size_t pos, size_t *out_row, size_t *out_col);
size_t cursor_find_line_start(const Buffer *buf, size_t pos);
size_t cursor_find_line_end(const Buffer *buf, size_t pos);

#endif /* MEMO_CURSOR_H */
