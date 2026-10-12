#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "terminal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

volatile sig_atomic_t g_terminal_resized = 0;
static struct termios g_orig_termios;
static int g_raw_mode_enabled = 0;

static void sigwinch_handler(int sig) {
    (void)sig;
    g_terminal_resized = 1;
}

static void sigcont_handler(int sig) {
    (void)sig;
    if (g_raw_mode_enabled) {
        terminal_enable_raw_mode();
        terminal_enter_alternate_screen();
    }
    g_terminal_resized = 1;
}

void terminal_setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = sigwinch_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, NULL);

    sa.sa_handler = sigcont_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCONT, &sa, NULL);
}

int terminal_enable_raw_mode(void) {
    if (!isatty(STDIN_FILENO)) {
        return -1;
    }

    if (!g_raw_mode_enabled) {
        if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) {
            return -1;
        }
    }

    struct termios raw = g_orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return -1;
    }

    g_raw_mode_enabled = 1;
    return 0;
}

void terminal_disable_raw_mode(void) {
    if (g_raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_raw_mode_enabled = 0;
    }
}

void terminal_enter_alternate_screen(void) {
    /* Enter alternate screen, enable cursor */
    const char *seq = "\x1b[?1049h\x1b[?25h";
    ssize_t written = write(STDOUT_FILENO, seq, strlen(seq));
    (void)written;
}

void terminal_exit_alternate_screen(void) {
    /* Show cursor, exit alternate screen */
    const char *seq = "\x1b[?25h\x1b[?1049l";
    ssize_t written = write(STDOUT_FILENO, seq, strlen(seq));
    (void)written;
}

static int get_cursor_position(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int terminal_get_window_size(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* Fallback: move cursor to bottom-right and query position */
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return get_cursor_position(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
