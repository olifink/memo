#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "input.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void input_init_terminal_protocol(void) {
    /* Enable Kitty keyboard protocol flags:
     * 1: Disambiguate escape codes
     * 2: Report event types (press, repeat, release)
     * 8: Report all keys as escape codes (including modifier keys)
     * 1 | 2 | 8 = 11 -> \x1b[>11u
     */
    const char *seq = "\x1b[>11u";
    ssize_t written = write(STDOUT_FILENO, seq, strlen(seq));
    (void)written;
}

void input_restore_terminal_protocol(void) {
    /* Pop flags from Kitty keyboard protocol */
    const char *seq = "\x1b[<u";
    ssize_t written = write(STDOUT_FILENO, seq, strlen(seq));
    (void)written;
}

static int read_byte_timeout(int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            return (int)c;
        }
    }
    return -1;
}

/* Parse CSI u escape sequence: e.g. "57445;1:1u", "97;3u", "57446u" */
static int parse_csi_u(const char *seq, InputEvent *ev) {
    int keycode = 0;
    int mod_val = 1;
    int event_type = 1;

    char *colon = strchr(seq, ':');
    if (colon) {
        event_type = colon[1] - '0';
        if (event_type < 1 || event_type > 3) event_type = 1;
    }

    char *semicolon = strchr(seq, ';');
    if (semicolon) {
        sscanf(semicolon + 1, "%d", &mod_val);
    }

    sscanf(seq, "%d", &keycode);

    ev->key = keycode;
    ev->type = (KeyEventType)event_type;

    /* Kitty protocol modifier offset is mod_val - 1 */
    int raw_mod = mod_val - 1;
    if (raw_mod < 0) raw_mod = 0;
    if (raw_mod & 1) ev->modifiers |= MOD_SHIFT;
    if (raw_mod & 2) ev->modifiers |= MOD_ALT;
    if (raw_mod & 4) ev->modifiers |= MOD_CTRL;
    if (raw_mod & 8) ev->modifiers |= MOD_SUPER;

    if (keycode == KEY_LEFT_ALT) {
        ev->modifiers |= MOD_LEFT_ALT | MOD_ALT;
    } else if (keycode == KEY_RIGHT_ALT) {
        ev->modifiers |= MOD_RIGHT_ALT | MOD_ALT;
    }

    return 1;
}

int input_read_event(InputEvent *ev, int timeout_ms) {
    memset(ev, 0, sizeof(*ev));
    ev->type = EVENT_PRESS;

    int c = read_byte_timeout(timeout_ms);
    if (c == -1) {
        return 0;
    }

    /* Not an escape sequence */
    if (c != '\x1b') {
        if (c == '\r') {
            ev->key = KEY_ENTER;
        } else if (c == 127 || c == 8) {
            ev->key = KEY_BACKSPACE;
        } else if (c < 32 && c != '\t' && c != '\n') {
            /* Control character Ctrl-A .. Ctrl-Z */
            ev->key = c;
            ev->modifiers |= MOD_CTRL;
        } else {
            /* Normal character (ASCII or UTF-8 lead byte) */
            ev->key = c;
            ev->utf8[0] = (char)c;
            ev->utf8_len = 1;

            /* Check UTF-8 multi-byte */
            int extra_bytes = 0;
            if ((c & 0xE0) == 0xC0) extra_bytes = 1;
            else if ((c & 0xF0) == 0xE0) extra_bytes = 2;
            else if ((c & 0xF8) == 0xF0) extra_bytes = 3;

            for (int i = 0; i < extra_bytes; i++) {
                int next = read_byte_timeout(20);
                if (next == -1) break;
                ev->utf8[ev->utf8_len++] = (char)next;
            }
            ev->utf8[ev->utf8_len] = '\0';
        }
        return 1;
    }

    /* We received an ESC ('\x1b') byte */
    int next1 = read_byte_timeout(25);
    if (next1 == -1) {
        /* Solitary ESC */
        ev->key = KEY_ESC;
        return 1;
    }

    if (next1 == '[') {
        /* CSI sequence */
        char seq[64];
        int len = 0;
        seq[len++] = '[';

        while (len < (int)sizeof(seq) - 2) {
            int b = read_byte_timeout(25);
            if (b == -1) break;
            seq[len++] = (char)b;
            /* Terminators: A..Z, a..z, ~ */
            if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || b == '~') {
                break;
            }
        }
        seq[len] = '\0';

        if (len > 1 && seq[len - 1] == 'u') {
            /* CSI u */
            seq[len - 1] = '\0'; /* Remove 'u' */
            parse_csi_u(seq + 1, ev);
            return 1;
        }

        /* Standard ANSI arrows */
        if (seq[1] == 'A') { ev->key = KEY_ARROW_UP; return 1; }
        if (seq[1] == 'B') { ev->key = KEY_ARROW_DOWN; return 1; }
        if (seq[1] == 'C') { ev->key = KEY_ARROW_RIGHT; return 1; }
        if (seq[1] == 'D') { ev->key = KEY_ARROW_LEFT; return 1; }
        if (seq[1] == 'H') { ev->key = KEY_HOME; return 1; }
        if (seq[1] == 'F') { ev->key = KEY_END; return 1; }

        if (seq[len - 1] == '~') {
            int num = 0;
            if (sscanf(seq + 1, "%d~", &num) == 1) {
                switch (num) {
                    case 1: case 7: ev->key = KEY_HOME; return 1;
                    case 4: case 8: ev->key = KEY_END; return 1;
                    case 3: ev->key = KEY_DELETE; return 1;
                    case 5: ev->key = KEY_PAGE_UP; return 1;
                    case 6: ev->key = KEY_PAGE_DOWN; return 1;
                }
            }
        }

        return 1;
    } else if (next1 == 'O') {
        int next2 = read_byte_timeout(25);
        if (next2 == 'H') { ev->key = KEY_HOME; return 1; }
        if (next2 == 'F') { ev->key = KEY_END; return 1; }
        return 1;
    } else {
        /* Alt + key in legacy terminals (e.g. ESC + 'f', ESC + 'b', ESC + 'm', etc.) */
        ev->key = next1;
        ev->modifiers |= MOD_ALT;
        ev->utf8[0] = (char)next1;
        ev->utf8[1] = '\0';
        ev->utf8_len = 1;
        return 1;
    }
}
