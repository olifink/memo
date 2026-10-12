#include "leap.h"

#include <ctype.h>
#include <string.h>

void leap_init(LeapState *ls) {
    if (!ls) return;
    ls->direction = LEAP_DIR_NONE;
    ls->query[0] = '\0';
    ls->query_len = 0;
    ls->origin_pos = 0;
    ls->current_match_pos = 0;
    ls->current_match_len = 0;
    ls->active = false;
    ls->wrapped = false;
    ls->matched = false;
}

void leap_start(LeapState *ls, LeapDirection dir, size_t current_pos) {
    if (!ls) return;
    ls->direction = dir;
    ls->query[0] = '\0';
    ls->query_len = 0;
    ls->origin_pos = current_pos;
    ls->current_match_pos = current_pos;
    ls->current_match_len = 0;
    ls->active = true;
    ls->wrapped = false;
    ls->matched = false;
}

static bool query_is_case_sensitive(const char *q, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (isupper((unsigned char)q[i])) {
            return true;
        }
    }
    return false;
}

static bool match_at(const Buffer *buf, size_t pos, const char *query, size_t qlen, bool case_sensitive) {
    for (size_t i = 0; i < qlen; i++) {
        char bc = buffer_char_at(buf, pos + i);
        char qc = query[i];
        if (case_sensitive) {
            if (bc != qc) return false;
        } else {
            if (tolower((unsigned char)bc) != tolower((unsigned char)qc)) return false;
        }
    }
    return true;
}

static bool find_forward(const Buffer *buf, const char *query, size_t qlen, size_t start, size_t *out_pos, bool *out_wrapped) {
    size_t total = buffer_length(buf);
    if (total < qlen || qlen == 0) return false;

    bool cs = query_is_case_sensitive(query, qlen);
    *out_wrapped = false;

    /* Search from start to end */
    for (size_t i = start; i <= total - qlen; i++) {
        if (match_at(buf, i, query, qlen, cs)) {
            *out_pos = i;
            return true;
        }
    }

    /* Wrap around from 0 to start */
    if (start > 0) {
        size_t limit = (start < total - qlen) ? start : (total - qlen);
        for (size_t i = 0; i < limit; i++) {
            if (match_at(buf, i, query, qlen, cs)) {
                *out_pos = i;
                *out_wrapped = true;
                return true;
            }
        }
    }

    return false;
}

static bool find_backward(const Buffer *buf, const char *query, size_t qlen, size_t start, size_t *out_pos, bool *out_wrapped) {
    size_t total = buffer_length(buf);
    if (total < qlen || qlen == 0) return false;

    bool cs = query_is_case_sensitive(query, qlen);
    *out_wrapped = false;

    /* Adjust start to not exceed total - qlen */
    if (start > total - qlen) {
        start = total - qlen;
    }

    /* Search backwards from start to 0 */
    for (size_t i = start + 1; i > 0; i--) {
        size_t p = i - 1;
        if (match_at(buf, p, query, qlen, cs)) {
            *out_pos = p;
            return true;
        }
    }

    /* Wrap around from end down to start */
    if (total >= qlen) {
        for (size_t i = total - qlen + 1; i > start + 1; i--) {
            size_t p = i - 1;
            if (match_at(buf, p, query, qlen, cs)) {
                *out_pos = p;
                *out_wrapped = true;
                return true;
            }
        }
    }

    return false;
}

static bool perform_search(LeapState *ls, const Buffer *buf, size_t from_pos, size_t *out_start, size_t *out_end) {
    if (ls->query_len == 0) return false;

    size_t found_pos = 0;
    bool wrapped = false;
    bool found = false;

    if (ls->direction == LEAP_DIR_FORWARD) {
        found = find_forward(buf, ls->query, ls->query_len, from_pos, &found_pos, &wrapped);
    } else if (ls->direction == LEAP_DIR_BACKWARD) {
        found = find_backward(buf, ls->query, ls->query_len, from_pos, &found_pos, &wrapped);
    }

    if (found) {
        ls->current_match_pos = found_pos;
        ls->current_match_len = ls->query_len;
        ls->wrapped = wrapped;
        ls->matched = true;
        if (out_start) *out_start = found_pos;
        if (out_end) *out_end = found_pos + ls->query_len;
        return true;
    } else {
        ls->matched = false;
        return false;
    }
}

bool leap_append_char(LeapState *ls, const Buffer *buf, char c, size_t *out_start, size_t *out_end) {
    if (!ls || ls->query_len >= sizeof(ls->query) - 1) return false;

    ls->query[ls->query_len++] = c;
    ls->query[ls->query_len] = '\0';

    return perform_search(ls, buf, ls->origin_pos, out_start, out_end);
}

bool leap_backspace(LeapState *ls, const Buffer *buf, size_t *out_start, size_t *out_end) {
    if (!ls || ls->query_len == 0) return false;

    ls->query[--ls->query_len] = '\0';

    if (ls->query_len == 0) {
        ls->matched = false;
        ls->current_match_pos = ls->origin_pos;
        ls->current_match_len = 0;
        if (out_start) *out_start = ls->origin_pos;
        if (out_end) *out_end = ls->origin_pos;
        return true;
    }

    return perform_search(ls, buf, ls->origin_pos, out_start, out_end);
}

bool leap_again(LeapState *ls, const Buffer *buf, size_t *out_start, size_t *out_end) {
    if (!ls || ls->query_len == 0) return false;

    size_t search_from = ls->current_match_pos;
    if (ls->direction == LEAP_DIR_FORWARD) {
        search_from += 1;
    } else if (ls->direction == LEAP_DIR_BACKWARD) {
        if (search_from > 0) search_from -= 1;
        else search_from = buffer_length(buf);
    }

    return perform_search(ls, buf, search_from, out_start, out_end);
}

bool leap_creep(LeapState *ls, const Buffer *buf, size_t current_pos, size_t *out_pos) {
    if (!ls || !buf) return false;
    size_t total = buffer_length(buf);

    if (ls->direction == LEAP_DIR_FORWARD) {
        if (current_pos < total) {
            *out_pos = current_pos + 1;
            return true;
        }
    } else if (ls->direction == LEAP_DIR_BACKWARD) {
        if (current_pos > 0) {
            *out_pos = current_pos - 1;
            return true;
        }
    }
    return false;
}

void leap_commit(LeapState *ls) {
    if (!ls) return;
    ls->active = false;
}

void leap_cancel(LeapState *ls, size_t *out_restore_pos) {
    if (!ls) return;
    if (out_restore_pos) *out_restore_pos = ls->origin_pos;
    ls->active = false;
}
