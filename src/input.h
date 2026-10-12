#ifndef MEMO_INPUT_H
#define MEMO_INPUT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    EVENT_PRESS = 1,
    EVENT_REPEAT = 2,
    EVENT_RELEASE = 3
} KeyEventType;

typedef enum {
    MOD_NONE      = 0,
    MOD_SHIFT     = (1 << 0),
    MOD_ALT       = (1 << 1),
    MOD_CTRL      = (1 << 2),
    MOD_SUPER     = (1 << 3),
    MOD_LEFT_ALT  = (1 << 4),
    MOD_RIGHT_ALT = (1 << 5)
} KeyModifiers;

typedef enum {
    KEY_NONE = 0,
    /* Special ASCII controls */
    KEY_CTRL_C = 3,
    KEY_TAB = 9,
    KEY_ENTER = 10,
    KEY_CTRL_Q = 17,
    KEY_CTRL_S = 19,
    KEY_ESC = 27,
    KEY_BACKSPACE = 127,

    /* Virtual functional keys */
    KEY_ARROW_UP = 1000,
    KEY_ARROW_DOWN,
    KEY_ARROW_LEFT,
    KEY_ARROW_RIGHT,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,

    /* Standalone modifier keys (CSI u) */
    KEY_LEFT_ALT = 57445,
    KEY_RIGHT_ALT = 57446
} KeyCode;

typedef struct {
    int key;              /* KeyCode or Unicode codepoint / ASCII char */
    uint32_t modifiers;   /* Bitmask of KeyModifiers */
    KeyEventType type;    /* Press, Repeat, or Release */
    char utf8[8];         /* UTF-8 bytes for printable characters */
    int utf8_len;
} InputEvent;

void input_init_terminal_protocol(void);
void input_restore_terminal_protocol(void);
int input_read_event(InputEvent *ev, int timeout_ms);

#endif /* MEMO_INPUT_H */
