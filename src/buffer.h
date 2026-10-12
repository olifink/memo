#ifndef MEMO_BUFFER_H
#define MEMO_BUFFER_H

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    BUF_ORIG,
    BUF_ADD
} BufferSource;

typedef struct Piece {
    BufferSource source;
    size_t offset;
    size_t length;
    struct Piece *prev;
    struct Piece *next;
} Piece;

typedef struct UndoNode {
    Piece *piece_head;
    size_t pencil_pos;
    size_t eraser_start;
    size_t eraser_end;
    struct UndoNode *prev;
    struct UndoNode *next;
} UndoNode;

typedef struct Buffer {
    char *orig_buf;
    size_t orig_size;
    size_t orig_cap;

    char *add_buf;
    size_t add_size;
    size_t add_cap;

    Piece *head;
    size_t total_length;
    bool dirty;

    UndoNode *undo_stack;
} Buffer;

Buffer *buffer_create(void);
void buffer_free(Buffer *buf);

int buffer_load_file(Buffer *buf, const char *filepath);
int buffer_save_file(Buffer *buf, const char *filepath);

size_t buffer_length(const Buffer *buf);
char buffer_char_at(const Buffer *buf, size_t pos);
size_t buffer_get_text(const Buffer *buf, size_t pos, size_t len, char *dest);
char *buffer_to_string(const Buffer *buf, size_t *out_len);

int buffer_insert(Buffer *buf, size_t pos, const char *text, size_t len);
int buffer_delete(Buffer *buf, size_t pos, size_t len);
int buffer_copy(Buffer *buf, size_t src_pos, size_t len, size_t dest_pos);
int buffer_move(Buffer *buf, size_t src_pos, size_t len, size_t dest_pos);

/* Undo management */
void buffer_save_undo_state(Buffer *buf, size_t pencil, size_t e_start, size_t e_end);
bool buffer_undo(Buffer *buf, size_t *pencil, size_t *e_start, size_t *e_end);

#endif /* MEMO_BUFFER_H */
