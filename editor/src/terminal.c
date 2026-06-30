#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <pty.h>
#include <fcntl.h>
#include "editor.h"

bool term_spawn(Editor *ed) {
  struct winsize ws = { .ws_row = (unsigned short)ed->term_rows,
                        .ws_col = (unsigned short)(ed->cols - SIDEBAR_WIDTH - 1),
                        .ws_xpixel = 0, .ws_ypixel = 0 };
  pid_t pid = forkpty(&ed->term_fd, NULL, NULL, &ws);
  if (pid < 0) return false;
  if (pid == 0) {
    const char *shell = getenv("SHELL");
    if (!shell) shell = "/bin/bash";
    execl(shell, shell, NULL);
    _exit(1);
  }
  ed->term_pid = pid;
  ed->term_total = 0;
  ed->term_head = 0;
  if (!ed->term_buf) ed->term_buf = calloc(1, TERM_BUF_SIZE);
  else memset(ed->term_buf, 0, TERM_BUF_SIZE);
  int flags = fcntl(ed->term_fd, F_GETFL, 0);
  fcntl(ed->term_fd, F_SETFL, flags | O_NONBLOCK);
  return true;
}

void term_write(Editor *ed, const char *data, int len) {
  if (ed->term_fd < 0) return;
  write(ed->term_fd, data, len);
}

void term_read(Editor *ed) {
  if (ed->term_fd < 0 || !ed->term_buf) return;
  char buf[4096];
  int n;
  while ((n = read(ed->term_fd, buf, sizeof(buf))) > 0) {
    for (int i = 0; i < n; i++) {
      int pos = (ed->term_head + ed->term_total) % TERM_BUF_SIZE;
      ed->term_buf[pos] = buf[i];
      if (ed->term_total < TERM_BUF_SIZE)
        ed->term_total++;
      else
        ed->term_head = (ed->term_head + 1) % TERM_BUF_SIZE;
    }
  }
}

void term_resize(Editor *ed) {
  if (ed->term_fd < 0) return;
  struct winsize ws = { .ws_row = (unsigned short)ed->term_rows,
                        .ws_col = (unsigned short)(ed->cols - SIDEBAR_WIDTH - 1),
                        .ws_xpixel = 0, .ws_ypixel = 0 };
  ioctl(ed->term_fd, TIOCSWINSZ, &ws);
  kill(ed->term_pid, SIGWINCH);
}

void term_kill(Editor *ed) {
  if (ed->term_fd >= 0) {
    close(ed->term_fd);
    ed->term_fd = -1;
  }
  if (ed->term_pid > 0) {
    kill(ed->term_pid, SIGTERM);
    ed->term_pid = 0;
  }
  free(ed->term_buf);
  ed->term_buf = NULL;
}
