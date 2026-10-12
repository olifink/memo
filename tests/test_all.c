#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/buffer.h"
#include "../src/cursor.h"
#include "../src/leap.h"

static int g_tests_run = 0;
#define TEST(name) static void test_##name(void); \
    static void run_##name(void) { \
        printf("  [RUN] %s\n", #name); \
        test_##name(); \
        printf("  [OK]  %s\n", #name); \
        g_tests_run++; \
    } \
    static void test_##name(void)

TEST(buffer_insert_and_text) {
    Buffer *buf = buffer_create();
    assert(buf != NULL);
    assert(buffer_length(buf) == 0);

    buffer_insert(buf, 0, "Hello world", 11);
    assert(buffer_length(buf) == 11);

    size_t len = 0;
    char *str = buffer_to_string(buf, &len);
    assert(strcmp(str, "Hello world") == 0);
    assert(len == 11);
    free(str);

    /* Insert in middle */
    buffer_insert(buf, 6, "brave new ", 10);
    str = buffer_to_string(buf, &len);
    assert(strcmp(str, "Hello brave new world") == 0);
    assert(len == 21);
    free(str);

    buffer_free(buf);
}

TEST(buffer_delete) {
    Buffer *buf = buffer_create();
    buffer_insert(buf, 0, "Hello brave new world", 21);

    /* Delete "brave new " */
    buffer_delete(buf, 6, 10);
    size_t len = 0;
    char *str = buffer_to_string(buf, &len);
    assert(strcmp(str, "Hello world") == 0);
    assert(len == 11);
    free(str);

    /* Delete from start */
    buffer_delete(buf, 0, 6);
    str = buffer_to_string(buf, &len);
    assert(strcmp(str, "world") == 0);
    assert(len == 5);
    free(str);

    buffer_free(buf);
}

TEST(buffer_move_and_copy) {
    Buffer *buf = buffer_create();
    buffer_insert(buf, 0, "ABCD1234", 8);

    /* Move "1234" to position 0 */
    buffer_move(buf, 4, 4, 0);
    char *str = buffer_to_string(buf, NULL);
    assert(strcmp(str, "1234ABCD") == 0);
    free(str);

    /* Copy "1234" to position 8 */
    buffer_copy(buf, 0, 4, 8);
    str = buffer_to_string(buf, NULL);
    assert(strcmp(str, "1234ABCD1234") == 0);
    free(str);

    buffer_free(buf);
}

TEST(buffer_undo) {
    Buffer *buf = buffer_create();
    size_t pencil = 0, es = 0, ee = 0;

    buffer_save_undo_state(buf, 0, 0, 0);
    buffer_insert(buf, 0, "first", 5);

    buffer_save_undo_state(buf, 5, 4, 5);
    buffer_insert(buf, 5, " second", 7);

    char *str = buffer_to_string(buf, NULL);
    assert(strcmp(str, "first second") == 0);
    free(str);

    /* Undo second */
    bool ok = buffer_undo(buf, &pencil, &es, &ee);
    assert(ok);
    assert(pencil == 5);
    str = buffer_to_string(buf, NULL);
    assert(strcmp(str, "first") == 0);
    free(str);

    /* Undo first */
    ok = buffer_undo(buf, &pencil, &es, &ee);
    assert(ok);
    assert(pencil == 0);
    assert(buffer_length(buf) == 0);

    /* No more undos */
    ok = buffer_undo(buf, &pencil, &es, &ee);
    assert(!ok);

    buffer_free(buf);
}

TEST(cursor_fundamental_rule) {
    CursorState cs;
    cursor_init(&cs);
    assert(cs.pencil_pos == 0);
    assert(cs.eraser_start == 0);
    assert(cs.eraser_end == 0);

    /* Typing 'H' (len 1) */
    cursor_after_insert(&cs, 1);
    assert(cs.pencil_pos == 1);
    assert(cs.eraser_start == 0);
    assert(cs.eraser_end == 1);

    /* Typing 'e' (len 1) */
    cursor_after_insert(&cs, 1);
    assert(cs.pencil_pos == 2);
    assert(cs.eraser_start == 1);
    assert(cs.eraser_end == 2);

    /* Typing "llo" (len 3) */
    cursor_after_insert(&cs, 3);
    assert(cs.pencil_pos == 5);
    assert(cs.eraser_start == 2);
    assert(cs.eraser_end == 5);

    /* Coalesce at match [10, 15) */
    cursor_coalesce_at(&cs, 10, 15);
    assert(cs.pencil_pos == 15);
    assert(cs.eraser_start == 10);
    assert(cs.eraser_end == 15);
    assert(!cs.has_span_selection);

    /* Span selection from 5 to 15 */
    cursor_set_span(&cs, 5, 15);
    assert(cs.has_span_selection);
    assert(cs.eraser_start == 5);
    assert(cs.eraser_end == 15);
    assert(cs.pencil_pos == 15);
}

TEST(cursor_arrow_navigation) {
    Buffer *buf = buffer_create();
    buffer_insert(buf, 0, "Line 1\nLine 2\nLine 3", 20);

    CursorState cs;
    cursor_init(&cs);
    assert(cs.pencil_pos == 0);

    /* Move right */
    cursor_move_right(&cs, buf);
    assert(cs.pencil_pos == 1);
    assert(cs.eraser_start == 0);
    assert(cs.eraser_end == 1);

    /* Move down to Line 2 */
    cursor_move_down(&cs, buf);
    size_t row, col;
    cursor_get_line_col(buf, cs.pencil_pos, &row, &col);
    assert(row == 1);
    assert(col == 1);

    /* Move down to Line 3 */
    cursor_move_down(&cs, buf);
    cursor_get_line_col(buf, cs.pencil_pos, &row, &col);
    assert(row == 2);
    assert(col == 1);

    /* Move up to Line 2 */
    cursor_move_up(&cs, buf);
    cursor_get_line_col(buf, cs.pencil_pos, &row, &col);
    assert(row == 1);
    assert(col == 1);

    buffer_free(buf);
}

TEST(leap_forward_and_backward) {
    Buffer *buf = buffer_create();
    const char *text = "The quick brown fox jumps over the lazy fox.";
    buffer_insert(buf, 0, text, strlen(text));

    LeapState ls;
    leap_init(&ls);

    /* 1. Forward leap to "fox" */
    leap_start(&ls, LEAP_DIR_FORWARD, 0);
    size_t s = 0, e = 0;
    bool ok = leap_append_char(&ls, buf, 'f', &s, &e);
    assert(ok);
    assert(s == 16); /* First 'f' in "fox" */

    ok = leap_append_char(&ls, buf, 'o', &s, &e);
    assert(ok);
    assert(s == 16);

    ok = leap_append_char(&ls, buf, 'x', &s, &e);
    assert(ok);
    assert(s == 16);
    assert(e == 19);

    /* 2. LEAP Again: should find second "fox" at 40 */
    ok = leap_again(&ls, buf, &s, &e);
    assert(ok);
    assert(s == 40);
    assert(e == 43);

    /* 3. LEAP Again: should wrap around to first "fox" at 16 */
    ok = leap_again(&ls, buf, &s, &e);
    assert(ok);
    assert(s == 16);
    assert(ls.wrapped);

    /* 4. Backward leap from end to "quick" */
    leap_start(&ls, LEAP_DIR_BACKWARD, strlen(text));
    ok = leap_append_char(&ls, buf, 'q', &s, &e);
    assert(ok);
    assert(s == 4); /* "quick" starts at index 4 */

    buffer_free(buf);
}

TEST(leap_case_sensitivity) {
    Buffer *buf = buffer_create();
    const char *text = "Cat and cat and CAT";
    buffer_insert(buf, 0, text, strlen(text));

    LeapState ls;
    leap_init(&ls);

    /* Lowercase "cat" matches case-insensitively -> matches first "Cat" */
    leap_start(&ls, LEAP_DIR_FORWARD, 0);
    size_t s = 0, e = 0;
    leap_append_char(&ls, buf, 'c', &s, &e);
    leap_append_char(&ls, buf, 'a', &s, &e);
    leap_append_char(&ls, buf, 't', &s, &e);
    assert(s == 0); /* Matched "Cat" at 0 */

    /* Uppercase "CAT" matches case-sensitively -> matches only "CAT" at 16 */
    leap_start(&ls, LEAP_DIR_FORWARD, 0);
    leap_append_char(&ls, buf, 'C', &s, &e);
    leap_append_char(&ls, buf, 'A', &s, &e);
    leap_append_char(&ls, buf, 'T', &s, &e);
    assert(s == 16); /* Matched "CAT" at 16 */

    buffer_free(buf);
}

TEST(leap_creep) {
    Buffer *buf = buffer_create();
    buffer_insert(buf, 0, "ABCDEF", 6);

    LeapState ls;
    leap_init(&ls);

    leap_start(&ls, LEAP_DIR_FORWARD, 2);
    size_t pos = 2;
    bool ok = leap_creep(&ls, buf, pos, &pos);
    assert(ok);
    assert(pos == 3);

    leap_start(&ls, LEAP_DIR_BACKWARD, 2);
    pos = 2;
    ok = leap_creep(&ls, buf, pos, &pos);
    assert(ok);
    assert(pos == 1);

    buffer_free(buf);
}

TEST(buffer_file_io) {
    const char *test_path = "test_sample.tmp";
    Buffer *buf = buffer_create();
    buffer_insert(buf, 0, "Line 1: Hello\nLine 2: World\n", 28);

    int res = buffer_save_file(buf, test_path);
    assert(res == 0);
    assert(!buf->dirty);
    buffer_free(buf);

    /* Reload into fresh buffer */
    Buffer *reloaded = buffer_create();
    res = buffer_load_file(reloaded, test_path);
    assert(res == 0);
    assert(buffer_length(reloaded) == 28);

    char *str = buffer_to_string(reloaded, NULL);
    assert(strcmp(str, "Line 1: Hello\nLine 2: World\n") == 0);
    free(str);

    buffer_free(reloaded);
    remove(test_path);
}

int main(void) {
    printf("Running memo automated test suite:\n");
    run_buffer_insert_and_text();
    run_buffer_delete();
    run_buffer_move_and_copy();
    run_buffer_undo();
    run_cursor_fundamental_rule();
    run_cursor_arrow_navigation();
    run_leap_forward_and_backward();
    run_leap_case_sensitivity();
    run_leap_creep();
    run_buffer_file_io();

    printf("\nAll %d tests passed successfully!\n", g_tests_run);
    return 0;
}
