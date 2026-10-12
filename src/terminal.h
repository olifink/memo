#ifndef MEMO_TERMINAL_H
#define MEMO_TERMINAL_H

#include <signal.h>

extern volatile sig_atomic_t g_terminal_resized;

int terminal_enable_raw_mode(void);
void terminal_disable_raw_mode(void);
int terminal_get_window_size(int *rows, int *cols);
void terminal_enter_alternate_screen(void);
void terminal_exit_alternate_screen(void);
void terminal_setup_signals(void);

#endif /* MEMO_TERMINAL_H */
