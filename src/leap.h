#ifndef MEMO_LEAP_H
#define MEMO_LEAP_H

#include "buffer.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    LEAP_DIR_NONE = 0,
    LEAP_DIR_FORWARD,
    LEAP_DIR_BACKWARD
} LeapDirection;

typedef struct LeapState {
    LeapDirection direction;
    char query[128];
    size_t query_len;
    size_t origin_pos;
    size_t current_match_pos;
    size_t current_match_len;
    bool active;
    bool wrapped;
    bool matched;
} LeapState;

void leap_init(LeapState *ls);
void leap_start(LeapState *ls, LeapDirection dir, size_t current_pos);
bool leap_append_char(LeapState *ls, const Buffer *buf, char c, size_t *out_start, size_t *out_end);
bool leap_backspace(LeapState *ls, const Buffer *buf, size_t *out_start, size_t *out_end);
bool leap_again(LeapState *ls, const Buffer *buf, size_t *out_start, size_t *out_end);
bool leap_creep(LeapState *ls, const Buffer *buf, size_t current_pos, size_t *out_pos);
void leap_commit(LeapState *ls);
void leap_cancel(LeapState *ls, size_t *out_restore_pos);

#endif /* MEMO_LEAP_H */
