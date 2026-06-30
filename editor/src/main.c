#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>
#include <poll.h>
#include "editor.h"

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");
  Editor ed;
  memset(&ed, 0, sizeof(ed));
  ed.running = true;
  ed.sidebar_entries = calloc(MAX_SIDEBAR, sizeof(SidebarEntry));

  ui_init(&ed);
  file_list_dir(&ed);

  if (argc > 1) new_tab(&ed, argv[1]);
  else new_tab(&ed, NULL);

  while (ed.running && ed.tab_count > 0) {
    term_read(&ed);
    ui_render(&ed);
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO; fds[0].events = POLLIN;
    int nfds = 1;
    if (ed.term_fd >= 0) {
      fds[1].fd = ed.term_fd; fds[1].events = POLLIN; nfds = 2;
    }
    poll(fds, nfds, ed.term_visible ? 50 : -1);
    if (nfds > 1 && (fds[1].revents & POLLIN)) term_read(&ed);
    if (fds[0].revents & POLLIN) {
      int ch = wgetch(ed.w_editor);
      if (ch != ERR) input_handle(&ed, ch);
    }
  }

  term_kill(&ed);
  for (int i = 0; i < ed.tab_count; i++) gb_free(&ed.tabs[i].gb);
  if (ed.w_term) delwin(ed.w_term);
  delwin(ed.w_tabs); delwin(ed.w_sidebar); delwin(ed.w_sep);
  delwin(ed.w_editor); delwin(ed.w_status);
  free(ed.sidebar_entries);
  endwin();
  return 0;
}
