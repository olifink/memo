#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "editor.h"
#include "input.h"
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *progname) {
    printf("Usage: %s [OPTIONS] [FILE]\n\n", progname);
    printf("A lightweight POSIX full-screen text editor inspired by the Canon Cat.\n\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help message and exit\n");
    printf("  -v, --version  Show version information and exit\n\n");
    printf("Navigation & Editing:\n");
    printf("  Right-ALT (held)        LEAP Forward (incremental search as you type)\n");
    printf("  Left-ALT (held)         LEAP Backward (incremental search as you type)\n");
    printf("  Left-ALT + Right-ALT    LEAP Both (select span between endpoints)\n");
    printf("  Arrow Keys              Local micro-navigation (Up/Down/Left/Right)\n");
    printf("  Space (during LEAP)     Creep forward/backward 1 character\n");
    printf("  Backspace / Delete      ERASE character or selected span\n");
    printf("  Alt-M                   MOVE selected span to cursor position\n");
    printf("  Alt-C                   COPY selected span to cursor position\n");
    printf("  Alt-U / Ctrl-Z          UNDO last editing operation\n");
    printf("  Ctrl-S                  Save buffer to disk\n");
    printf("  Ctrl-Q                  Quit memo\n");
}

int main(int argc, char *argv[]) {
    const char *filepath = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("memo 0.1.0 (Canon Cat text editor)\n");
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            filepath = argv[i];
        }
    }

    terminal_setup_signals();

    if (terminal_enable_raw_mode() == -1) {
        perror("Failed to enable raw terminal mode");
        return 1;
    }

    terminal_enter_alternate_screen();
    input_init_terminal_protocol();

    Editor ed;
    if (editor_init(&ed, filepath) == 0) {
        editor_run(&ed);
    }
    editor_free(&ed);

    input_restore_terminal_protocol();
    terminal_exit_alternate_screen();
    terminal_disable_raw_mode();

    return 0;
}
