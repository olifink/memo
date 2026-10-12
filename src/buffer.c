#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "buffer.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define INITIAL_CAPACITY 4096

static Piece *piece_create(BufferSource src, size_t offset, size_t length) {
    Piece *p = malloc(sizeof(Piece));
    if (!p) return NULL;
    p->source = src;
    p->offset = offset;
    p->length = length;
    p->prev = NULL;
    p->next = NULL;
    return p;
}

static Piece *piece_clone_chain(const Piece *head) {
    if (!head) return NULL;
    Piece *new_head = NULL;
    Piece *cur_tail = NULL;

    for (const Piece *curr = head; curr != NULL; curr = curr->next) {
        Piece *p = piece_create(curr->source, curr->offset, curr->length);
        if (!p) return NULL;
        if (!new_head) {
            new_head = p;
            cur_tail = p;
        } else {
            cur_tail->next = p;
            p->prev = cur_tail;
            cur_tail = p;
        }
    }
    return new_head;
}

static void piece_free_chain(Piece *head) {
    while (head) {
        Piece *next = head->next;
        free(head);
        head = next;
    }
}

Buffer *buffer_create(void) {
    Buffer *buf = calloc(1, sizeof(Buffer));
    if (!buf) return NULL;

    buf->orig_cap = INITIAL_CAPACITY;
    buf->orig_buf = malloc(buf->orig_cap);

    buf->add_cap = INITIAL_CAPACITY;
    buf->add_buf = malloc(buf->add_cap);

    buf->head = NULL;
    buf->total_length = 0;
    buf->dirty = false;
    buf->undo_stack = NULL;

    return buf;
}

void buffer_free(Buffer *buf) {
    if (!buf) return;

    piece_free_chain(buf->head);

    UndoNode *u = buf->undo_stack;
    while (u) {
        UndoNode *next = u->next;
        piece_free_chain(u->piece_head);
        free(u);
        u = next;
    }

    free(buf->orig_buf);
    free(buf->add_buf);
    free(buf);
}

size_t buffer_length(const Buffer *buf) {
    return buf ? buf->total_length : 0;
}

static char *get_source_ptr(const Buffer *buf, BufferSource src) {
    return src == BUF_ORIG ? buf->orig_buf : buf->add_buf;
}

char buffer_char_at(const Buffer *buf, size_t pos) {
    if (!buf || pos >= buf->total_length) return '\0';

    size_t acc = 0;
    for (Piece *p = buf->head; p != NULL; p = p->next) {
        if (pos < acc + p->length) {
            size_t rel = pos - acc;
            char *base = get_source_ptr(buf, p->source);
            return base[p->offset + rel];
        }
        acc += p->length;
    }
    return '\0';
}

size_t buffer_get_text(const Buffer *buf, size_t pos, size_t len, char *dest) {
    if (!buf || !dest || pos >= buf->total_length || len == 0) return 0;

    size_t req_end = pos + len;
    size_t remaining = len;
    size_t copied = 0;
    size_t acc = 0;

    for (Piece *p = buf->head; p != NULL && remaining > 0; p = p->next) {
        size_t p_start = acc;
        size_t p_end = acc + p->length;

        if (pos < p_end && req_end > p_start) {
            size_t src_start = (pos > p_start) ? (pos - p_start) : 0;
            size_t chunk_len = p->length - src_start;
            if (chunk_len > remaining) chunk_len = remaining;

            char *base = get_source_ptr(buf, p->source);
            memcpy(dest + copied, base + p->offset + src_start, chunk_len);

            copied += chunk_len;
            remaining -= chunk_len;
        }
        acc += p->length;
    }
    return copied;
}

char *buffer_to_string(const Buffer *buf, size_t *out_len) {
    if (!buf) return NULL;
    char *str = malloc(buf->total_length + 1);
    if (!str) return NULL;

    size_t written = buffer_get_text(buf, 0, buf->total_length, str);
    str[written] = '\0';
    if (out_len) *out_len = written;
    return str;
}

static void ensure_add_capacity(Buffer *buf, size_t needed) {
    if (buf->add_size + needed > buf->add_cap) {
        while (buf->add_size + needed > buf->add_cap) {
            buf->add_cap *= 2;
        }
        buf->add_buf = realloc(buf->add_buf, buf->add_cap);
    }
}

void buffer_save_undo_state(Buffer *buf, size_t pencil, size_t e_start, size_t e_end) {
    if (!buf) return;
    UndoNode *node = malloc(sizeof(UndoNode));
    if (!node) return;

    node->piece_head = piece_clone_chain(buf->head);
    node->pencil_pos = pencil;
    node->eraser_start = e_start;
    node->eraser_end = e_end;
    node->prev = NULL;
    node->next = buf->undo_stack;

    if (buf->undo_stack) {
        buf->undo_stack->prev = node;
    }
    buf->undo_stack = node;
}

bool buffer_undo(Buffer *buf, size_t *pencil, size_t *e_start, size_t *e_end) {
    if (!buf || !buf->undo_stack) return false;

    UndoNode *node = buf->undo_stack;
    buf->undo_stack = node->next;
    if (buf->undo_stack) {
        buf->undo_stack->prev = NULL;
    }

    piece_free_chain(buf->head);
    buf->head = node->piece_head;

    size_t len = 0;
    for (Piece *p = buf->head; p != NULL; p = p->next) {
        len += p->length;
    }
    buf->total_length = len;
    buf->dirty = true;

    if (pencil) *pencil = node->pencil_pos;
    if (e_start) *e_start = node->eraser_start;
    if (e_end) *e_end = node->eraser_end;

    free(node);
    return true;
}

int buffer_insert(Buffer *buf, size_t pos, const char *text, size_t len) {
    if (!buf || !text || len == 0) return 0;
    if (pos > buf->total_length) pos = buf->total_length;

    ensure_add_capacity(buf, len);
    size_t add_offset = buf->add_size;
    memcpy(buf->add_buf + add_offset, text, len);
    buf->add_size += len;

    Piece *new_piece = piece_create(BUF_ADD, add_offset, len);
    if (!new_piece) return -1;

    buf->dirty = true;
    buf->total_length += len;

    /* Empty buffer */
    if (!buf->head) {
        buf->head = new_piece;
        return 0;
    }

    /* Insert at beginning */
    if (pos == 0) {
        new_piece->next = buf->head;
        buf->head->prev = new_piece;
        buf->head = new_piece;
        return 0;
    }

    /* Locate insertion piece */
    size_t acc = 0;
    Piece *curr = buf->head;
    while (curr) {
        if (pos == acc + curr->length) {
            /* Insert right after curr */
            new_piece->next = curr->next;
            new_piece->prev = curr;
            if (curr->next) curr->next->prev = new_piece;
            curr->next = new_piece;
            return 0;
        } else if (pos < acc + curr->length) {
            /* Split curr */
            size_t left_len = pos - acc;
            size_t right_len = curr->length - left_len;

            Piece *right_piece = piece_create(curr->source, curr->offset + left_len, right_len);
            if (!right_piece) return -1;

            curr->length = left_len;

            right_piece->next = curr->next;
            right_piece->prev = new_piece;
            if (curr->next) curr->next->prev = right_piece;

            new_piece->next = right_piece;
            new_piece->prev = curr;
            curr->next = new_piece;
            return 0;
        }
        acc += curr->length;
        if (!curr->next) break;
        curr = curr->next;
    }

    /* Append to tail */
    curr->next = new_piece;
    new_piece->prev = curr;
    return 0;
}

int buffer_delete(Buffer *buf, size_t pos, size_t len) {
    if (!buf || len == 0 || pos >= buf->total_length) return 0;
    if (pos + len > buf->total_length) {
        len = buf->total_length - pos;
    }

    size_t del_start = pos;
    size_t del_end = pos + len;
    size_t acc = 0;

    Piece *curr = buf->head;
    while (curr && acc < del_end) {
        size_t p_start = acc;
        size_t p_end = acc + curr->length;
        acc += curr->length;

        if (p_end <= del_start || p_start >= del_end) {
            curr = curr->next;
            continue;
        }

        /* Overlap exists */
        if (p_start >= del_start && p_end <= del_end) {
            /* Entire piece deleted */
            Piece *to_del = curr;
            curr = curr->next;
            if (to_del->prev) to_del->prev->next = to_del->next;
            else buf->head = to_del->next;
            if (to_del->next) to_del->next->prev = to_del->prev;
            free(to_del);
            continue;
        } else if (del_start > p_start && del_end < p_end) {
            /* Deletion inside single piece -> split into two */
            size_t left_len = del_start - p_start;
            size_t right_len = p_end - del_end;

            Piece *right = piece_create(curr->source, curr->offset + (del_end - p_start), right_len);
            curr->length = left_len;

            right->next = curr->next;
            right->prev = curr;
            if (curr->next) curr->next->prev = right;
            curr->next = right;
            curr = right->next;
            continue;
        } else if (del_start <= p_start && del_end < p_end) {
            /* Truncate piece start */
            size_t cut = del_end - p_start;
            curr->offset += cut;
            curr->length -= cut;
            curr = curr->next;
            continue;
        } else if (del_start > p_start && del_end >= p_end) {
            /* Truncate piece end */
            curr->length = del_start - p_start;
            curr = curr->next;
            continue;
        }
    }

    buf->total_length -= len;
    buf->dirty = true;
    return 0;
}

int buffer_copy(Buffer *buf, size_t src_pos, size_t len, size_t dest_pos) {
    if (!buf || len == 0 || src_pos >= buf->total_length) return 0;
    char *text = malloc(len);
    if (!text) return -1;

    size_t copied = buffer_get_text(buf, src_pos, len, text);
    int res = buffer_insert(buf, dest_pos, text, copied);
    free(text);
    return res;
}

int buffer_move(Buffer *buf, size_t src_pos, size_t len, size_t dest_pos) {
    if (!buf || len == 0 || src_pos >= buf->total_length) return 0;
    char *text = malloc(len);
    if (!text) return -1;

    size_t copied = buffer_get_text(buf, src_pos, len, text);
    buffer_delete(buf, src_pos, copied);

    size_t target = dest_pos;
    if (target > src_pos) {
        if (target >= src_pos + copied) {
            target -= copied;
        } else {
            target = src_pos;
        }
    }

    int res = buffer_insert(buf, target, text, copied);
    free(text);
    return res;
}

int buffer_load_file(Buffer *buf, const char *filepath) {
    if (!buf || !filepath) return -1;

    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        /* File doesn't exist yet - initialize empty buffer */
        return 0;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        return -1;
    }

    size_t size = (size_t)st.st_size;
    if (size > buf->orig_cap) {
        buf->orig_cap = size + 1024;
        buf->orig_buf = realloc(buf->orig_buf, buf->orig_cap);
    }

    ssize_t nread = 0;
    size_t total = 0;
    while (total < size && (nread = read(fd, buf->orig_buf + total, size - total)) > 0) {
        total += (size_t)nread;
    }
    close(fd);

    buf->orig_size = total;
    piece_free_chain(buf->head);

    if (total > 0) {
        buf->head = piece_create(BUF_ORIG, 0, total);
        buf->total_length = total;
    } else {
        buf->head = NULL;
        buf->total_length = 0;
    }

    buf->dirty = false;
    return 0;
}

int buffer_save_file(Buffer *buf, const char *filepath) {
    if (!buf || !filepath) return -1;

    /* Write to temporary file first, then atomic rename */
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.memo_tmp.XXXXXX", filepath);

    int fd = mkstemp(tmp_path);
    if (fd == -1) {
        /* Fallback if mkstemp in path fails */
        snprintf(tmp_path, sizeof(tmp_path), ".memo_tmp.XXXXXX");
        fd = mkstemp(tmp_path);
        if (fd == -1) return -1;
    }

    for (Piece *p = buf->head; p != NULL; p = p->next) {
        char *base = get_source_ptr(buf, p->source);
        size_t written = 0;
        while (written < p->length) {
            ssize_t nw = write(fd, base + p->offset + written, p->length - written);
            if (nw <= 0) {
                close(fd);
                unlink(tmp_path);
                return -1;
            }
            written += (size_t)nw;
        }
    }

    fsync(fd);
    close(fd);

    if (rename(tmp_path, filepath) == -1) {
        unlink(tmp_path);
        return -1;
    }

    buf->dirty = false;
    return 0;
}
