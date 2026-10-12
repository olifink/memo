#include "cursor.h"

void cursor_init(CursorState *cs) {
    if (!cs) return;
    cs->pencil_pos = 0;
    cs->eraser_start = 0;
    cs->eraser_end = 0;
    cs->has_span_selection = false;
    cs->leap_origin = 0;
    cs->preferred_col = 0;
}

void cursor_after_insert(CursorState *cs, size_t inserted_len) {
    if (!cs) return;
    cs->eraser_start = cs->pencil_pos;
    cs->pencil_pos += inserted_len;
    cs->eraser_end = cs->pencil_pos;
    cs->has_span_selection = false;
    cs->preferred_col = -1;
}

void cursor_after_erase(CursorState *cs, const Buffer *buf) {
    if (!cs) return;
    size_t total = buffer_length(buf);
    if (cs->pencil_pos > total) cs->pencil_pos = total;

    if (cs->pencil_pos > 0) {
        cs->eraser_start = cs->pencil_pos - 1;
        cs->eraser_end = cs->pencil_pos;
    } else {
        cs->eraser_start = 0;
        cs->eraser_end = 0;
    }
    cs->has_span_selection = false;
    cs->preferred_col = -1;
}

void cursor_coalesce_at(CursorState *cs, size_t start, size_t end) {
    if (!cs) return;
    cs->eraser_start = start;
    cs->eraser_end = end;
    cs->pencil_pos = end;
    cs->has_span_selection = false;
    cs->preferred_col = -1;
}

void cursor_set_span(CursorState *cs, size_t start, size_t end) {
    if (!cs) return;
    size_t s = (start < end) ? start : end;
    size_t e = (start < end) ? end : start;

    cs->eraser_start = s;
    cs->eraser_end = e;
    cs->pencil_pos = end;
    cs->has_span_selection = (s != e);
}

void cursor_clear_selection(CursorState *cs) {
    if (!cs) return;
    cs->has_span_selection = false;
    if (cs->pencil_pos > 0) {
        cs->eraser_start = cs->pencil_pos - 1;
        cs->eraser_end = cs->pencil_pos;
    } else {
        cs->eraser_start = 0;
        cs->eraser_end = 0;
    }
}

size_t cursor_find_line_start(const Buffer *buf, size_t pos) {
    if (!buf || pos == 0) return 0;
    size_t total = buffer_length(buf);
    if (pos > total) pos = total;

    size_t p = pos;
    while (p > 0) {
        if (buffer_char_at(buf, p - 1) == '\n') {
            return p;
        }
        p--;
    }
    return 0;
}

size_t cursor_find_line_end(const Buffer *buf, size_t pos) {
    if (!buf) return 0;
    size_t total = buffer_length(buf);
    size_t p = pos;
    while (p < total) {
        if (buffer_char_at(buf, p) == '\n') {
            return p;
        }
        p++;
    }
    return total;
}

void cursor_get_line_col(const Buffer *buf, size_t pos, size_t *out_row, size_t *out_col) {
    if (!buf) {
        if (out_row) *out_row = 0;
        if (out_col) *out_col = 0;
        return;
    }

    size_t row = 0;
    size_t lstart = 0;
    size_t total = buffer_length(buf);
    if (pos > total) pos = total;

    for (size_t i = 0; i < pos; i++) {
        if (buffer_char_at(buf, i) == '\n') {
            row++;
            lstart = i + 1;
        }
    }

    if (out_row) *out_row = row;
    if (out_col) *out_col = pos - lstart;
}

void cursor_move_left(CursorState *cs, const Buffer *buf) {
    (void)buf;
    if (!cs) return;
    cursor_clear_selection(cs);
    if (cs->pencil_pos > 0) {
        cs->pencil_pos--;
        cs->eraser_start = (cs->pencil_pos > 0) ? cs->pencil_pos - 1 : 0;
        cs->eraser_end = cs->pencil_pos;
    }
    cs->preferred_col = -1;
}

void cursor_move_right(CursorState *cs, const Buffer *buf) {
    if (!cs || !buf) return;
    cursor_clear_selection(cs);
    size_t total = buffer_length(buf);
    if (cs->pencil_pos < total) {
        cs->pencil_pos++;
        cs->eraser_start = cs->pencil_pos - 1;
        cs->eraser_end = cs->pencil_pos;
    }
    cs->preferred_col = -1;
}

void cursor_move_up(CursorState *cs, const Buffer *buf) {
    if (!cs || !buf) return;
    cursor_clear_selection(cs);

    size_t cur_row, cur_col;
    cursor_get_line_col(buf, cs->pencil_pos, &cur_row, &cur_col);
    if (cur_row == 0) return; /* Already on first line */

    if (cs->preferred_col == -1) {
        cs->preferred_col = (int)cur_col;
    }

    /* Find start of current line */
    size_t lstart = cursor_find_line_start(buf, cs->pencil_pos);
    /* Previous line ends at lstart - 1 */
    size_t prev_line_end = lstart - 1;
    size_t prev_line_start = cursor_find_line_start(buf, prev_line_end);
    size_t prev_line_len = prev_line_end - prev_line_start;

    size_t target_col = (size_t)cs->preferred_col;
    if (target_col > prev_line_len) target_col = prev_line_len;

    cs->pencil_pos = prev_line_start + target_col;
    cs->eraser_start = (cs->pencil_pos > 0) ? cs->pencil_pos - 1 : 0;
    cs->eraser_end = cs->pencil_pos;
}

void cursor_move_down(CursorState *cs, const Buffer *buf) {
    if (!cs || !buf) return;
    cursor_clear_selection(cs);

    size_t cur_row, cur_col;
    cursor_get_line_col(buf, cs->pencil_pos, &cur_row, &cur_col);

    if (cs->preferred_col == -1) {
        cs->preferred_col = (int)cur_col;
    }

    size_t lend = cursor_find_line_end(buf, cs->pencil_pos);
    size_t total = buffer_length(buf);
    if (lend >= total) return; /* Already on last line */

    size_t next_line_start = lend + 1;
    size_t next_line_end = cursor_find_line_end(buf, next_line_start);
    size_t next_line_len = next_line_end - next_line_start;

    size_t target_col = (size_t)cs->preferred_col;
    if (target_col > next_line_len) target_col = next_line_len;

    cs->pencil_pos = next_line_start + target_col;
    cs->eraser_start = (cs->pencil_pos > 0) ? cs->pencil_pos - 1 : 0;
    cs->eraser_end = cs->pencil_pos;
}
