#ifndef MEMO_EDITOR_H
#define MEMO_EDITOR_H

#include "buffer.h"
#include "cursor.h"
#include "input.h"
#include "leap.h"
#include "render.h"

typedef struct Editor {
    Buffer *buf;
    CursorState cursor;
    LeapState leap;
    RenderContext render;
    bool running;
    const char *filepath;
    bool left_alt_held;
    bool right_alt_held;
    bool leap_quasimode_held;
    int quit_confirm_count;
} Editor;

int editor_init(Editor *ed, const char *filepath);
void editor_free(Editor *ed);
void editor_run(Editor *ed);
void editor_process_event(Editor *ed, const InputEvent *ev);

#endif /* MEMO_EDITOR_H */
