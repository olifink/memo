#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "editor.h"
#include "terminal.h"

#include <ctype.h>
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

static LeapDirection decide_leap_direction(const Buffer *buf, size_t pos, char c) {
    size_t total = buffer_length(buf);
    if (total == 0) return LEAP_DIR_FORWARD;
    if (pos >= total) return LEAP_DIR_BACKWARD;
    if (pos == 0) return LEAP_DIR_FORWARD;

    size_t fwd_dist = (size_t)-1;
    size_t back_dist = (size_t)-1;
    char target_lower = (char)tolower((unsigned char)c);

    for (size_t i = pos; i < total; i++) {
        if ((char)tolower((unsigned char)buffer_char_at(buf, i)) == target_lower) {
            fwd_dist = i - pos;
            break;
        }
    }

    for (size_t i = pos; i > 0; i--) {
        if ((char)tolower((unsigned char)buffer_char_at(buf, i - 1)) == target_lower) {
            back_dist = pos - (i - 1);
            break;
        }
    }

    if (fwd_dist == (size_t)-1 && back_dist != (size_t)-1) {
        return LEAP_DIR_BACKWARD;
    }
    if (back_dist == (size_t)-1 && fwd_dist != (size_t)-1) {
        return LEAP_DIR_FORWARD;
    }
    if (fwd_dist != (size_t)-1 && back_dist != (size_t)-1) {
        return (fwd_dist <= back_dist) ? LEAP_DIR_FORWARD : LEAP_DIR_BACKWARD;
    }

    return LEAP_DIR_FORWARD;
}

void editor_process_event(Editor *ed, const InputEvent *ev) {
    if (ev->key != KEY_CTRL_Q) {
        ed->quit_confirm_count = 0;
    }

    /* 1. Handle CSI u Left-Alt / Right-Alt keycode events */
    if (ev->key == KEY_LEFT_ALT) {
        if (ev->type == EVENT_PRESS || ev->type == EVENT_REPEAT) {
            ed->left_alt_held = true;
            if (ed->right_alt_held) {
                /* Both ALTs held: engage LEAP Both / Span selection */
                cursor_set_span(&ed->cursor, ed->cursor.leap_origin, ed->cursor.pencil_pos);
                render_set_status_msg(&ed->render, "LEAP-LEAP: Span selection engaged.");
            } else if (ed->leap.active && ed->leap.query_len > 0) {
                ed->leap.direction = LEAP_DIR_BACKWARD;
                handle_leap_again(ed);
            } else {
                ed->cursor.leap_origin = ed->cursor.pencil_pos;
                leap_start(&ed->leap, LEAP_DIR_BACKWARD, ed->cursor.pencil_pos);
                ed->leap_quasimode_held = true;
            }
        } else if (ev->type == EVENT_RELEASE) {
            ed->left_alt_held = false;
            if (!ed->right_alt_held && ed->leap.active) {
                leap_commit(&ed->leap);
                ed->leap_quasimode_held = false;
            }
        }
        return;
    }

    if (ev->key == KEY_RIGHT_ALT) {
        if (ev->type == EVENT_PRESS || ev->type == EVENT_REPEAT) {
            ed->right_alt_held = true;
            if (ed->left_alt_held) {
                cursor_set_span(&ed->cursor, ed->cursor.leap_origin, ed->cursor.pencil_pos);
                render_set_status_msg(&ed->render, "LEAP-LEAP: Span selection engaged.");
            } else if (ed->leap.active && ed->leap.query_len > 0) {
                ed->leap.direction = LEAP_DIR_FORWARD;
                handle_leap_again(ed);
            } else {
                ed->cursor.leap_origin = ed->cursor.pencil_pos;
                leap_start(&ed->leap, LEAP_DIR_FORWARD, ed->cursor.pencil_pos);
                ed->leap_quasimode_held = true;
            }
        } else if (ev->type == EVENT_RELEASE) {
            ed->right_alt_held = false;
            if (!ed->left_alt_held && ed->leap.active) {
                leap_commit(&ed->leap);
                ed->leap_quasimode_held = false;
            }
        }
        return;
    }

    /* 2. When LEAP is active */
    if (ed->leap.active) {
        /* If in quasimode (started by holding Alt without modifier release events),
         * receiving a key WITHOUT MOD_ALT means the user has released Alt!
         * Commit LEAP and fall through to process the non-Alt key normally.
         */
        if (ed->leap_quasimode_held && !ed->left_alt_held && !ed->right_alt_held && !(ev->modifiers & MOD_ALT)) {
            leap_commit(&ed->leap);
            ed->leap_quasimode_held = false;
            /* Do not return! Fall through so the non-Alt key executes immediately (e.g. Backspace erases match, char inserts) */
        } else {
            /* Still in active LEAP mode */
            if (ev->key == KEY_ESC || ev->key == KEY_ENTER) {
                leap_commit(&ed->leap);
                ed->leap_quasimode_held = false;
                return;
            }

            /* Tab: cycle through matches in current direction (LEAP Again) */
            if (ev->key == KEY_TAB) {
                handle_leap_again(ed);
                return;
            }

            /* Alt-Right: cycle forward */
            if ((ev->modifiers & MOD_ALT) && ev->key == KEY_ARROW_RIGHT) {
                ed->leap.direction = LEAP_DIR_FORWARD;
                handle_leap_again(ed);
                return;
            }

            /* Alt-Left: cycle backward */
            if ((ev->modifiers & MOD_ALT) && ev->key == KEY_ARROW_LEFT) {
                ed->leap.direction = LEAP_DIR_BACKWARD;
                handle_leap_again(ed);
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

            /* Character typed while leaping */
            if (ev->utf8_len > 0 && ev->key >= 32 && ev->key <= 126) {
                size_t s, e;
                bool found = leap_append_char(&ed->leap, ed->buf, (char)ev->key, &s, &e);
                if (found) {
                    cursor_coalesce_at(&ed->cursor, s, e);
                } else {
                    /* If appending failed, but key is identical to the last search character,
                     * treat it as LEAP Again (cycle to next occurrence of current pattern)
                     */
                    if (ed->leap.query_len > 1 && ed->leap.query[ed->leap.query_len - 1] == (char)ev->key) {
                        ed->leap.query[--ed->leap.query_len] = '\0';
                        handle_leap_again(ed);
                    }
                }
                return;
            }

            /* Arrow keys pressed while leaping: commit and fall through */
            if (ev->key == KEY_ARROW_LEFT || ev->key == KEY_ARROW_RIGHT ||
                ev->key == KEY_ARROW_UP || ev->key == KEY_ARROW_DOWN) {
                leap_commit(&ed->leap);
                ed->leap_quasimode_held = false;
                /* fall through to arrow navigation */
            }
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

    if (ev->key == KEY_CTRL_S || ((ev->modifiers & MOD_ALT) && (ev->key == 'w' || ev->key == 'W') && ed->cursor.has_span_selection)) {
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

    /* Modal search prompts: Ctrl-F (Forward) and Ctrl-R (Backward) */
    if ((ev->modifiers & MOD_CTRL) && (ev->key == 'f' || ev->key == 6)) {
        ed->cursor.leap_origin = ed->cursor.pencil_pos;
        leap_start(&ed->leap, LEAP_DIR_FORWARD, ed->cursor.pencil_pos);
        ed->leap_quasimode_held = false;
        return;
    }

    if ((ev->modifiers & MOD_CTRL) && (ev->key == 'r' || ev->key == 18)) {
        ed->cursor.leap_origin = ed->cursor.pencil_pos;
        leap_start(&ed->leap, LEAP_DIR_BACKWARD, ed->cursor.pencil_pos);
        ed->leap_quasimode_held = false;
        return;
    }

    /* Alt-S / Ctrl-Space for Span selection */
    if (((ev->modifiers & MOD_ALT) && (ev->key == 's' || ev->key == 'S')) ||
        (ev->key == 0 && (ev->modifiers & MOD_CTRL))) {
        cursor_set_span(&ed->cursor, ed->cursor.leap_origin, ed->cursor.pencil_pos);
        render_set_status_msg(&ed->render, "Span selection set (%zu to %zu)",
                              ed->cursor.eraser_start, ed->cursor.eraser_end);
        return;
    }

    /* Alt-M: MOVE (only when selection exists) */
    if ((ev->modifiers & MOD_ALT) && (ev->key == 'm' || ev->key == 'M') && ed->cursor.has_span_selection) {
        buffer_save_undo_state(ed->buf, ed->cursor.pencil_pos, ed->cursor.eraser_start, ed->cursor.eraser_end);
        size_t span_len = ed->cursor.eraser_end - ed->cursor.eraser_start;
        buffer_move(ed->buf, ed->cursor.eraser_start, span_len, ed->cursor.pencil_pos);
        cursor_coalesce_at(&ed->cursor, ed->cursor.pencil_pos, ed->cursor.pencil_pos + span_len);
        render_set_status_msg(&ed->render, "MOVE: Relocated %zu bytes.", span_len);
        return;
    }

    /* Alt-C: COPY (only when selection exists) */
    if ((ev->modifiers & MOD_ALT) && (ev->key == 'c' || ev->key == 'C') && ed->cursor.has_span_selection) {
        buffer_save_undo_state(ed->buf, ed->cursor.pencil_pos, ed->cursor.eraser_start, ed->cursor.eraser_end);
        size_t span_len = ed->cursor.eraser_end - ed->cursor.eraser_start;
        buffer_copy(ed->buf, ed->cursor.eraser_start, span_len, ed->cursor.pencil_pos);
        cursor_coalesce_at(&ed->cursor, ed->cursor.pencil_pos, ed->cursor.pencil_pos + span_len);
        render_set_status_msg(&ed->render, "COPY: Duplicated %zu bytes.", span_len);
        return;
    }

    /* Alt-U / Ctrl-Z: UNDO */
    if (((ev->modifiers & MOD_ALT) && (ev->key == 'u' || ev->key == 'U')) ||
        ((ev->modifiers & MOD_CTRL) && (ev->key == 'z' || ev->key == 26)) ||
        ev->key == 26) {
        if (buffer_undo(ed->buf, &ed->cursor.pencil_pos, &ed->cursor.eraser_start, &ed->cursor.eraser_end)) {
            render_set_status_msg(&ed->render, "UNDO successful.");
        } else {
            render_set_status_msg(&ed->render, "Already at oldest change.");
        }
        return;
    }

    /* Explicit direction triggers: Alt-F (Forward) and Alt-B (Backward) */
    if ((ev->modifiers & MOD_ALT) && (ev->key == 'f' || ev->key == 'F')) {
        ed->cursor.leap_origin = ed->cursor.pencil_pos;
        leap_start(&ed->leap, LEAP_DIR_FORWARD, ed->cursor.pencil_pos);
        ed->leap_quasimode_held = true;
        size_t s, e;
        if (leap_append_char(&ed->leap, ed->buf, (char)ev->key, &s, &e)) {
            cursor_coalesce_at(&ed->cursor, s, e);
        }
        return;
    }

    if ((ev->modifiers & MOD_ALT) && (ev->key == 'b' || ev->key == 'B')) {
        ed->cursor.leap_origin = ed->cursor.pencil_pos;
        leap_start(&ed->leap, LEAP_DIR_BACKWARD, ed->cursor.pencil_pos);
        ed->leap_quasimode_held = true;
        size_t s, e;
        if (leap_append_char(&ed->leap, ed->buf, (char)ev->key, &s, &e)) {
            cursor_coalesce_at(&ed->cursor, s, e);
        }
        return;
    }

    /* Direct Alt + <character> LEAP start (quasimode fallback for terminals without CSI u) */
    if ((ev->modifiers & MOD_ALT) && ev->utf8_len > 0 && ev->key >= 32 && ev->key <= 126) {
        LeapDirection dir = decide_leap_direction(ed->buf, ed->cursor.pencil_pos, (char)ev->key);
        ed->cursor.leap_origin = ed->cursor.pencil_pos;
        leap_start(&ed->leap, dir, ed->cursor.pencil_pos);
        ed->leap_quasimode_held = true;
        size_t s, e;
        bool found = leap_append_char(&ed->leap, ed->buf, (char)ev->key, &s, &e);
        if (found) {
            cursor_coalesce_at(&ed->cursor, s, e);
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
