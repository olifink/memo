#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "editor.h"
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int editor_init(Editor *ed, const char *filepath) {
    memset(ed, 0, sizeof(*ed));
    ed->buf = buffer_create();
    if (!ed->buf) return -1;

    cursor_init(&ed->cursor);
    leap_init(&ed->leap);
    render_init(&ed->render);

    ed->filepath = filepath;
    ed->render.filename = filepath;
    ed->running = true;
    ed->quit_confirm_count = 0;

    if (filepath) {
        if (buffer_load_file(ed->buf, filepath) == 0) {
            render_set_status_msg(&ed->render, "Loaded %s (%zu bytes)", filepath, buffer_length(ed->buf));
        } else {
            render_set_status_msg(&ed->render, "New file: %s", filepath);
        }
    } else {
        render_set_status_msg(&ed->render, "Welcome to memo | Hold Right-Alt for FWD LEAP, Left-Alt for BACK LEAP");
    }

    terminal_get_window_size(&ed->render.screen_rows, &ed->render.screen_cols);
    return 0;
}

void editor_free(Editor *ed) {
    if (ed->buf) {
        buffer_free(ed->buf);
        ed->buf = NULL;
    }
}

static void handle_leap_again(Editor *ed) {
    size_t s, e;
    if (leap_again(&ed->leap, ed->buf, &s, &e)) {
        cursor_coalesce_at(&ed->cursor, s, e);
    }
}

void editor_process_event(Editor *ed, const InputEvent *ev) {
    if (ev->key != KEY_CTRL_Q) {
        ed->quit_confirm_count = 0;
    }

    /* 1. Handle CSI u Left-Alt / Right-Alt keycode events */
    if (ev->key == KEY_LEFT_ALT) {
        if (ev->type == EVENT_PRESS) {
            ed->left_alt_held = true;
            if (ed->right_alt_held) {
                /* Both ALTs held: engage LEAP Both / Span selection */
                cursor_set_span(&ed->cursor, ed->cursor.leap_origin, ed->cursor.pencil_pos);
                render_set_status_msg(&ed->render, "LEAP-LEAP: Span selection engaged.");
            } else {
                ed->cursor.leap_origin = ed->cursor.pencil_pos;
                leap_start(&ed->leap, LEAP_DIR_BACKWARD, ed->cursor.pencil_pos);
            }
        } else if (ev->type == EVENT_RELEASE) {
            ed->left_alt_held = false;
            if (!ed->right_alt_held && ed->leap.active) {
                leap_commit(&ed->leap);
            }
        }
        return;
    }

    if (ev->key == KEY_RIGHT_ALT) {
        if (ev->type == EVENT_PRESS) {
            ed->right_alt_held = true;
            if (ed->left_alt_held) {
                cursor_set_span(&ed->cursor, ed->cursor.leap_origin, ed->cursor.pencil_pos);
                render_set_status_msg(&ed->render, "LEAP-LEAP: Span selection engaged.");
            } else {
                ed->cursor.leap_origin = ed->cursor.pencil_pos;
                leap_start(&ed->leap, LEAP_DIR_FORWARD, ed->cursor.pencil_pos);
            }
        } else if (ev->type == EVENT_RELEASE) {
            ed->right_alt_held = false;
            if (!ed->left_alt_held && ed->leap.active) {
                leap_commit(&ed->leap);
            }
        }
        return;
    }

    /* 2. When LEAP is active, process keys into incremental search term */
    if (ed->leap.active) {
        if (ev->key == KEY_ESC || ev->key == KEY_ENTER) {
            leap_commit(&ed->leap);
            return;
        }

        if (ev->key == KEY_BACKSPACE) {
            size_t s, e;
            if (leap_backspace(&ed->leap, ed->buf, &s, &e)) {
                cursor_coalesce_at(&ed->cursor, s, e);
            }
            return;
        }

        if (ev->key == ' ' && ed->leap.query_len == 0) {
            /* Creep navigation: single character nudge */
            size_t new_pos = ed->cursor.pencil_pos;
            if (leap_creep(&ed->leap, ed->buf, ed->cursor.pencil_pos, &new_pos)) {
                cursor_coalesce_at(&ed->cursor, (new_pos > 0 ? new_pos - 1 : 0), new_pos);
            }
            return;
        }

        /* Legacy Alt-F / Alt-B while LEAP active triggers LEAP Again */
        if ((ev->modifiers & MOD_ALT) && (ev->key == 'f' || ev->key == 'b')) {
            handle_leap_again(ed);
            return;
        }

        /* Printable character typed during LEAP */
        if (ev->utf8_len > 0 && ev->key >= 32 && ev->key <= 126) {
            size_t s, e;
            if (leap_append_char(&ed->leap, ed->buf, (char)ev->key, &s, &e)) {
                cursor_coalesce_at(&ed->cursor, s, e);
            }
            return;
        }
    }

    /* 3. Global Control & Editing Commands */
    if (ev->key == KEY_CTRL_Q) {
        if (ed->buf->dirty && ed->quit_confirm_count == 0) {
            render_set_status_msg(&ed->render, "WARNING: Unsaved changes! Press Ctrl-Q again to quit.");
            ed->quit_confirm_count++;
            return;
        }
        ed->running = false;
        return;
    }

    if (ev->key == KEY_CTRL_S || ((ev->modifiers & MOD_ALT) && ev->key == 'w')) {
        if (ed->filepath) {
            if (buffer_save_file(ed->buf, ed->filepath) == 0) {
                render_set_status_msg(&ed->render, "Saved %zu bytes to %s", buffer_length(ed->buf), ed->filepath);
            } else {
                render_set_status_msg(&ed->render, "Error saving file: %s", ed->filepath);
            }
        } else {
            render_set_status_msg(&ed->render, "No filename specified. Run memo with filename argument.");
        }
        return;
    }

    /* Alt-F / Alt-B fallback trigger for LEAP (for terminals without modifier release) */
    if ((ev->modifiers & MOD_ALT) && ev->key == 'f') {
        if (ed->leap.active && ed->leap.direction == LEAP_DIR_FORWARD) {
            handle_leap_again(ed);
        } else {
            ed->cursor.leap_origin = ed->cursor.pencil_pos;
            leap_start(&ed->leap, LEAP_DIR_FORWARD, ed->cursor.pencil_pos);
        }
        return;
    }

    if ((ev->modifiers & MOD_ALT) && ev->key == 'b') {
        if (ed->leap.active && ed->leap.direction == LEAP_DIR_BACKWARD) {
            handle_leap_again(ed);
        } else {
            ed->cursor.leap_origin = ed->cursor.pencil_pos;
            leap_start(&ed->leap, LEAP_DIR_BACKWARD, ed->cursor.pencil_pos);
        }
        return;
    }

    /* Alt-S / Ctrl-Space for Span selection */
    if (((ev->modifiers & MOD_ALT) && ev->key == 's') || (ev->key == 0 && (ev->modifiers & MOD_CTRL))) {
        cursor_set_span(&ed->cursor, ed->cursor.leap_origin, ed->cursor.pencil_pos);
        render_set_status_msg(&ed->render, "Span selection set (%zu to %zu)",
                              ed->cursor.eraser_start, ed->cursor.eraser_end);
        return;
    }

    /* Alt-M: MOVE */
    if ((ev->modifiers & MOD_ALT) && (ev->key == 'm' || ev->key == 'M')) {
        if (ed->cursor.has_span_selection) {
            buffer_save_undo_state(ed->buf, ed->cursor.pencil_pos, ed->cursor.eraser_start, ed->cursor.eraser_end);
            size_t span_len = ed->cursor.eraser_end - ed->cursor.eraser_start;
            buffer_move(ed->buf, ed->cursor.eraser_start, span_len, ed->cursor.pencil_pos);
            cursor_coalesce_at(&ed->cursor, ed->cursor.pencil_pos, ed->cursor.pencil_pos + span_len);
            render_set_status_msg(&ed->render, "MOVE: Relocated %zu bytes.", span_len);
        } else {
            render_set_status_msg(&ed->render, "No selection to MOVE. Hold both ALTs or press Alt-S first.");
        }
        return;
    }

    /* Alt-C: COPY */
    if ((ev->modifiers & MOD_ALT) && (ev->key == 'c' || ev->key == 'C')) {
        if (ed->cursor.has_span_selection) {
            buffer_save_undo_state(ed->buf, ed->cursor.pencil_pos, ed->cursor.eraser_start, ed->cursor.eraser_end);
            size_t span_len = ed->cursor.eraser_end - ed->cursor.eraser_start;
            buffer_copy(ed->buf, ed->cursor.eraser_start, span_len, ed->cursor.pencil_pos);
            cursor_coalesce_at(&ed->cursor, ed->cursor.pencil_pos, ed->cursor.pencil_pos + span_len);
            render_set_status_msg(&ed->render, "COPY: Duplicated %zu bytes.", span_len);
        } else {
            render_set_status_msg(&ed->render, "No selection to COPY.");
        }
        return;
    }

    /* Alt-U / Ctrl-Z: UNDO */
    if (((ev->modifiers & MOD_ALT) && ev->key == 'u') ||
        ((ev->modifiers & MOD_CTRL) && ev->key == 'z') ||
        ev->key == 26) {
        if (buffer_undo(ed->buf, &ed->cursor.pencil_pos, &ed->cursor.eraser_start, &ed->cursor.eraser_end)) {
            render_set_status_msg(&ed->render, "UNDO successful.");
        } else {
            render_set_status_msg(&ed->render, "Already at oldest change.");
        }
        return;
    }

    /* 4. Local Arrow Navigation */
    if (ev->key == KEY_ARROW_LEFT) {
        cursor_move_left(&ed->cursor, ed->buf);
        return;
    }
    if (ev->key == KEY_ARROW_RIGHT) {
        cursor_move_right(&ed->cursor, ed->buf);
        return;
    }
    if (ev->key == KEY_ARROW_UP) {
        cursor_move_up(&ed->cursor, ed->buf);
        return;
    }
    if (ev->key == KEY_ARROW_DOWN) {
        cursor_move_down(&ed->cursor, ed->buf);
        return;
    }

    /* 5. ERASE (Backspace / Delete) */
    if (ev->key == KEY_BACKSPACE || ev->key == KEY_DELETE) {
        buffer_save_undo_state(ed->buf, ed->cursor.pencil_pos, ed->cursor.eraser_start, ed->cursor.eraser_end);
        if (ed->cursor.has_span_selection) {
            size_t len = ed->cursor.eraser_end - ed->cursor.eraser_start;
            buffer_delete(ed->buf, ed->cursor.eraser_start, len);
            ed->cursor.pencil_pos = ed->cursor.eraser_start;
            cursor_after_erase(&ed->cursor, ed->buf);
        } else {
            if (ed->cursor.eraser_start < ed->cursor.eraser_end) {
                size_t len = ed->cursor.eraser_end - ed->cursor.eraser_start;
                buffer_delete(ed->buf, ed->cursor.eraser_start, len);
                ed->cursor.pencil_pos = ed->cursor.eraser_start;
                cursor_after_erase(&ed->cursor, ed->buf);
            } else if (ed->cursor.pencil_pos > 0) {
                buffer_delete(ed->buf, ed->cursor.pencil_pos - 1, 1);
                ed->cursor.pencil_pos--;
                cursor_after_erase(&ed->cursor, ed->buf);
            }
        }
        return;
    }

    /* 6. Enter Key */
    if (ev->key == KEY_ENTER) {
        buffer_save_undo_state(ed->buf, ed->cursor.pencil_pos, ed->cursor.eraser_start, ed->cursor.eraser_end);
        if (ed->cursor.has_span_selection) {
            buffer_delete(ed->buf, ed->cursor.eraser_start, ed->cursor.eraser_end - ed->cursor.eraser_start);
            ed->cursor.pencil_pos = ed->cursor.eraser_start;
        }
        buffer_insert(ed->buf, ed->cursor.pencil_pos, "\n", 1);
        cursor_after_insert(&ed->cursor, 1);
        return;
    }

    /* 7. Tab Key */
    if (ev->key == KEY_TAB) {
        buffer_save_undo_state(ed->buf, ed->cursor.pencil_pos, ed->cursor.eraser_start, ed->cursor.eraser_end);
        if (ed->cursor.has_span_selection) {
            buffer_delete(ed->buf, ed->cursor.eraser_start, ed->cursor.eraser_end - ed->cursor.eraser_start);
            ed->cursor.pencil_pos = ed->cursor.eraser_start;
        }
        buffer_insert(ed->buf, ed->cursor.pencil_pos, "    ", 4);
        cursor_after_insert(&ed->cursor, 4);
        return;
    }

    /* 8. Modeless Typing (Printable ASCII and UTF-8 characters) */
    if (ev->utf8_len > 0 && !(ev->modifiers & (MOD_CTRL | MOD_ALT))) {
        buffer_save_undo_state(ed->buf, ed->cursor.pencil_pos, ed->cursor.eraser_start, ed->cursor.eraser_end);
        if (ed->cursor.has_span_selection) {
            buffer_delete(ed->buf, ed->cursor.eraser_start, ed->cursor.eraser_end - ed->cursor.eraser_start);
            ed->cursor.pencil_pos = ed->cursor.eraser_start;
        }
        buffer_insert(ed->buf, ed->cursor.pencil_pos, ev->utf8, ev->utf8_len);
        cursor_after_insert(&ed->cursor, ev->utf8_len);
        return;
    }
}

void editor_run(Editor *ed) {
    while (ed->running) {
        if (g_terminal_resized) {
            g_terminal_resized = 0;
            terminal_get_window_size(&ed->render.screen_rows, &ed->render.screen_cols);
        }

        render_screen(&ed->render, ed->buf, &ed->cursor, &ed->leap);

        InputEvent ev;
        if (input_read_event(&ev, 100)) {
            editor_process_event(ed, &ev);
        }
    }
}
