#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "editor.h"
static bool try_complete(Editor *ed);

static void tab_switch(Editor *ed, int idx) {
  if (idx < 0 || idx >= ed->tab_count) return;
  ed->active_tab = idx;
  ed->term_focus = false;
}

static void tab_close(Editor *ed, int idx) {
  if (idx < 0 || idx >= ed->tab_count) return;
  gb_free(&ed->tabs[idx].gb);
  memmove(&ed->tabs[idx], &ed->tabs[idx + 1],
          (ed->tab_count - idx - 1) * sizeof(Tab));
  ed->tab_count--;
  if (ed->active_tab >= ed->tab_count)
    ed->active_tab = ed->tab_count > 0 ? ed->tab_count - 1 : 0;
}

bool new_tab(Editor *ed, const char *path) {
  if (ed->tab_count >= MAX_TABS) return false;
  // check if file already open
  if (path && path[0]) {
    for (int i = 0; i < ed->tab_count; i++) {
      if (ed->tabs[i].filepath[0] && !strcmp(ed->tabs[i].filepath, path)) {
        ed->active_tab = i;
        return true;
      }
    }
  }
  Tab *t = &ed->tabs[ed->tab_count];
  memset(t, 0, sizeof(Tab));
  t->gb = gb_new();
  if (path && path[0]) {
    if (!file_open_tab(t, path)) {
      gb_free(&t->gb);
      return false;
    }
  }
  ed->active_tab = ed->tab_count;
  ed->tab_count++;
  return true;
}

static void term_toggle(Editor *ed) {
  if (!ed->term_visible) {
    // show terminal
    if (ed->term_fd < 0) {
      ed->term_rows = ed->rows / 4;
      if (ed->term_rows < 3) ed->term_rows = 3;
      term_spawn(ed);
    }
    ed->term_visible = true;
    ed->term_focus = true;

    // create terminal window
    int content_rows = ed->rows - 2 - ed->term_rows;
    int term_y = 1 + content_rows;
    ed->w_term = newwin(ed->term_rows, ed->cols - SIDEBAR_WIDTH - 1,
                        term_y, SIDEBAR_WIDTH + 1);

    // shrink editor/sidebar/sep
    wresize(ed->w_sidebar, content_rows, SIDEBAR_WIDTH);
    wresize(ed->w_sep, content_rows, 1);
    wresize(ed->w_editor, content_rows, ed->cols - SIDEBAR_WIDTH - 1);
    term_resize(ed);
  } else {
    // hide terminal
    ed->term_visible = false;
    ed->term_focus = false;
    delwin(ed->w_term);
    ed->w_term = NULL;
    int content_rows = ed->rows - 2;
    wresize(ed->w_sidebar, content_rows, SIDEBAR_WIDTH);
    wresize(ed->w_sep, content_rows, 1);
    wresize(ed->w_editor, content_rows, ed->cols - SIDEBAR_WIDTH - 1);
  }
}

static void handle_mouse(Editor *ed, MEVENT *ev) {
  int x = ev->x, y = ev->y;

  if (y == ed->rows - 1) return; // status bar

  // Tab bar
  if (y == 0) {
    int tab_idx = x / TAB_WIDTH;
    int off = x % TAB_WIDTH;
    if (tab_idx < ed->tab_count) {
      if (off >= TAB_WIDTH - 3) // click on " × "
        tab_close(ed, tab_idx);
      else
        tab_switch(ed, tab_idx);
    }
    return;
  }

  // Sidebar
  if (x < SIDEBAR_WIDTH) {
    int ent = ed->sidebar_scroll + y - 2;
    if (ent >= 0 && ent < ed->sidebar_count) {
      SidebarEntry *e = &ed->sidebar_entries[ent];
      ed->sidebar_select = ent;
      if (e->is_dir)
        sidebar_toggle(ed, ent);
      else
        new_tab(ed, e->path);
    }
    return;
  }

  // Separator
  if (x == SIDEBAR_WIDTH) return;

  // Terminal area (if visible)
  if (ed->term_visible) {
    int content_rows = ed->rows - 2 - ed->term_rows;
    int term_y = 1 + content_rows;
    if (y >= term_y && y < term_y + ed->term_rows) {
      ed->term_focus = true;
      return;
    }
  }

  // Editor area
  if (y > 0 && y < ed->rows - 1) {
    ed->term_focus = false;
    Tab *t = &ed->tabs[ed->active_tab];
    if (!t->gb.buf) return;

    int content_rows = ed->rows - 2 - (ed->term_visible ? ed->term_rows : 0);
    int editor_y = y - 1;
    if (editor_y >= content_rows) return;

    int click_row = editor_y + t->scroll_y;
    char *text = gb_get_text(&t->gb);
    int line = 0;
    size_t pos = 0;
    for (; pos < strlen(text) && line < click_row; pos++) {
      if (text[pos] == '\n') line++;
    }
    size_t line_end = pos;
    while (line_end < strlen(text) && text[line_end] != '\n') line_end++;

    int gutter = GUTTER_WIDTH;
    int click_col = x - SIDEBAR_WIDTH - 1 - gutter + t->scroll_x;
    if (click_col < 0) click_col = 0;

    size_t byte_off = 0;
    int vcol = 0;
    while (vcol < click_col && pos + byte_off < line_end) {
      vcol++;
      unsigned char c = text[pos + byte_off];
      if (c < 0x80) byte_off += 1;
      else if ((c & 0xE0) == 0xC0) byte_off += 2;
      else if ((c & 0xF0) == 0xE0) byte_off += 3;
      else byte_off += 4;
    }
    gb_set_cursor(&t->gb, pos + byte_off);
    t->cx = click_col;
    t->cy = click_row;
    free(text);
  }
}

static void handle_keyboard(Editor *ed, int ch) {
  Tab *t = &ed->tabs[ed->active_tab];
  switch (ch) {
  case 17: // Ctrl+Q
    ed->running = false;
    break;
  case 19: { // Ctrl+S
    if (!t->filepath[0]) {
      echo(); curs_set(1);
      leaveok(ed->w_status, FALSE);
      char buf[MAX_PATH] = {0};
      mvwprintw(ed->w_status, 0, 0, " Save as: ");
      wgetnstr(ed->w_status, buf, MAX_PATH - 1);
      noecho(); curs_set(2);
      leaveok(ed->w_status, TRUE);
      if (!buf[0]) break;
      strncpy(t->filepath, buf, MAX_PATH - 1);
      t->filepath[MAX_PATH - 1] = '\0';
      t->filename = strrchr(t->filepath, '/');
      t->filename = (char *)(t->filename ? t->filename + 1 : t->filepath);
    }
    file_save_tab(t);
    break;
  }
  case 23: // Ctrl+W
    tab_close(ed, ed->active_tab);
    if (ed->tab_count == 0) ed->running = false;
    break;
  case 14: // Ctrl+N
    new_tab(ed, NULL);
    break;
  case 15: { // Ctrl+O
    echo();
    curs_set(1);
    char buf[MAX_PATH] = {0};
    mvwprintw(ed->w_status, 0, 0, " Open file: ");
    wgetnstr(ed->w_status, buf, MAX_PATH - 1);
    noecho();
    curs_set(2);
    if (buf[0]) new_tab(ed, buf);
    break;
  }
  case 20: // Ctrl+T - toggle Terminal
    term_toggle(ed);
    break;
  case KEY_BACKSPACE:
  case 8:
  case 127:
    if (gb_cursor_pos(&t->gb) > 0) {
      gb_delete_before(&t->gb, 1);
      t->dirty = true;
      if (t->cx > 0) t->cx--;
      else {
        t->cy--;
        char *text = gb_get_text(&t->gb);
        size_t cp = gb_cursor_pos(&t->gb);
        int col = 0;
        while (cp > 0 && text[cp - 1] != '\n') { cp--; col++; }
        t->cx = col;
        free(text);
      }
    }
    break;
  case KEY_DC:
    if (gb_cursor_pos(&t->gb) < gb_length(&t->gb)) {
      gb_delete_after(&t->gb, 1);
      t->dirty = true;
    }
    break;
  case '\n':
  case KEY_ENTER:
    gb_insert(&t->gb, "\n", 1);
    t->dirty = true;
    t->cy++;
    t->cx = 0;
    break;
  case '\t':
    if (!try_complete(ed)) {
      gb_insert(&t->gb, "  ", 2);
      t->dirty = true;
      t->cx += 2;
    }
    break;
  case KEY_LEFT:
    if (gb_cursor_pos(&t->gb) > 0) {
      gb_set_cursor(&t->gb, gb_cursor_pos(&t->gb) - 1);
      if (t->cx > 0) t->cx--;
      else {
        t->cy--;
        char *text = gb_get_text(&t->gb);
        size_t cp = gb_cursor_pos(&t->gb);
        int col = 0;
        while (cp > 0 && text[cp - 1] != '\n') { cp--; col++; }
        t->cx = col;
        free(text);
      }
    }
    break;
  case KEY_RIGHT:
    if (gb_cursor_pos(&t->gb) < gb_length(&t->gb)) {
      gb_set_cursor(&t->gb, gb_cursor_pos(&t->gb) + 1);
      char *text = gb_get_text(&t->gb);
      size_t cp = gb_cursor_pos(&t->gb);
      if (cp > 0 && text[cp - 1] == '\n') { t->cy++; t->cx = 0; }
      else t->cx++;
      free(text);
    }
    break;
  case KEY_UP: {
    char *text = gb_get_text(&t->gb);
    size_t cp = gb_cursor_pos(&t->gb);
    size_t ls = cp;
    while (ls > 0 && text[ls - 1] != '\n') ls--;
    if (ls == 0) { free(text); break; }
    size_t pe = ls - 1, ps = pe;
    while (ps > 0 && text[ps - 1] != '\n') ps--;
    size_t pl = pe - ps;
    size_t tc = t->cx;
    if (tc > pl) tc = pl;
    gb_set_cursor(&t->gb, ps + tc);
    t->cy--;
    t->cx = tc;
    if (t->cy < t->scroll_y) t->scroll_y = t->cy;
    free(text);
    break;
  }
  case KEY_DOWN: {
    char *text = gb_get_text(&t->gb);
    size_t cp = gb_cursor_pos(&t->gb);
    size_t le = cp;
    while (le < strlen(text) && text[le] != '\n') le++;
    if (le >= strlen(text)) { free(text); break; }
    size_t ns = le + 1, ne = ns;
    while (ne < strlen(text) && text[ne] != '\n') ne++;
    size_t nl = ne - ns;
    size_t tc = t->cx;
    if (tc > nl) tc = nl;
    gb_set_cursor(&t->gb, ns + tc);
    t->cy++;
    t->cx = tc;
    free(text);
    break;
  }
  case KEY_HOME:
    if (t->cx > 0) {
      gb_set_cursor(&t->gb, gb_cursor_pos(&t->gb) - t->cx);
      t->cx = 0;
    }
    break;
  case KEY_END: {
    char *text = gb_get_text(&t->gb);
    size_t cp = gb_cursor_pos(&t->gb);
    size_t eol = cp;
    while (eol < strlen(text) && text[eol] != '\n') eol++;
    int move = eol - cp;
    gb_set_cursor(&t->gb, eol);
    t->cx += move;
    free(text);
    break;
  }
  case KEY_PPAGE: {
    int move = (ed->rows - 2) / 2;
    if (move < 1) move = 1;
    for (int i = 0; i < move && t->cy > 0; i++) {
      char *text = gb_get_text(&t->gb);
      size_t cp = gb_cursor_pos(&t->gb);
      size_t ls = cp;
      while (ls > 0 && text[ls - 1] != '\n') ls--;
      if (ls == 0) { gb_set_cursor(&t->gb, 0); t->cy = 0; t->cx = 0; free(text); break; }
      size_t pe = ls - 1, ps = pe;
      while (ps > 0 && text[ps - 1] != '\n') ps--;
      size_t pl = pe - ps;
      size_t tc = t->cx;
      if (tc > pl) tc = pl;
      gb_set_cursor(&t->gb, ps + tc);
      t->cy--;
      t->cx = tc;
      free(text);
    }
    if (t->cy < t->scroll_y) t->scroll_y = t->cy;
    break;
  }
  case KEY_NPAGE: {
    int move = (ed->rows - 2) / 2;
    if (move < 1) move = 1;
    for (int i = 0; i < move; i++) {
      char *text = gb_get_text(&t->gb);
      size_t cp = gb_cursor_pos(&t->gb);
      size_t le = cp;
      while (le < strlen(text) && text[le] != '\n') le++;
      if (le >= strlen(text)) { free(text); break; }
      size_t ns = le + 1, ne = ns;
      while (ne < strlen(text) && text[ne] != '\n') ne++;
      size_t nl = ne - ns;
      size_t tc = t->cx;
      if (tc > nl) tc = nl;
      gb_set_cursor(&t->gb, ns + tc);
      t->cy++;
      t->cx = tc;
      free(text);
    }
    break;
  }
  default:
    if (ch >= 32 && ch < 127) {
      gb_insert(&t->gb, (char[]){ (char)ch, '\0' }, 1);
      t->dirty = true;
      t->cx++;
    } else if ((unsigned)ch >= 0x80) {
      if (ch < 256) {
        // Raw UTF-8 byte (ncurses returns individual bytes on some terminals)
        gb_insert(&t->gb, (char[]){ (char)ch, '\0' }, 1);
        t->dirty = true;
	t->cx++;
      } else {
        // Decoded Unicode code point → UTF-8
        unsigned char utf8[5];
        int len;
        if (ch < 0x800) {
          utf8[0] = 0xC0 | (ch >> 6);
          utf8[1] = 0x80 | (ch & 0x3F);
          len = 2;
        } else if (ch < 0x10000) {
          utf8[0] = 0xE0 | (ch >> 12);
          utf8[1] = 0x80 | ((ch >> 6) & 0x3F);
          utf8[2] = 0x80 | (ch & 0x3F);
          len = 3;
        } else if (ch <= 0x10FFFF) {
          utf8[0] = 0xF0 | (ch >> 18);
          utf8[1] = 0x80 | ((ch >> 12) & 0x3F);
          utf8[2] = 0x80 | ((ch >> 6) & 0x3F);
          utf8[3] = 0x80 | (ch & 0x3F);
          len = 4;
        } else break;
        gb_insert(&t->gb, (char *)utf8, len);
        t->dirty = true;
        t->cx++;
      }
    }
    break;
  }
}

/* ── Code Completion ───────────────────────────────── */
#define MAX_CANDIDATES 128

static bool is_word_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

static int collect_candidates(const char *text, const char *prefix,
                               char (*candidates)[256]) {
  int count = 0;
  size_t plen = strlen(prefix);
  size_t len = strlen(text);
  for (size_t i = 0; i < len && count < MAX_CANDIDATES; ) {
    if (!is_word_char(text[i])) { i++; continue; }
    size_t start = i;
    while (i < len && is_word_char(text[i])) i++;
    size_t wlen = i - start;
    if (wlen > plen && memcmp(text + start, prefix, plen) == 0) {
      size_t cl = wlen < 255 ? wlen : 255;
      char buf[256];
      memcpy(buf, text + start, cl); buf[cl] = '\0';
      bool dup = false;
      for (int j = 0; j < count; j++)
        if (!strcmp(candidates[j], buf)) { dup = true; break; }
      if (!dup) { memcpy(candidates[count], buf, cl + 1); count++; }
    }
  }
  return count;
}

static void completion_insert(Editor *ed, const char *word,
                               size_t word_start, size_t cursor) {
  Tab *t = &ed->tabs[ed->active_tab];
  gb_set_cursor(&t->gb, word_start);
  if (cursor > word_start)
    gb_delete_after(&t->gb, cursor - word_start);
  gb_insert(&t->gb, word, strlen(word));

  // Recalculate cy/cx from new cursor position
  size_t pos = gb_cursor_pos(&t->gb);
  char *text = gb_get_text(&t->gb);
  t->cy = 0; t->cx = 0;
  for (size_t i = 0; i < pos && text[i]; i++) {
    if (text[i] == '\n') { t->cy++; t->cx = 0; }
    else t->cx++;
  }
  free(text);
  t->dirty = true;
}

static bool completion_popup(Editor *ed, char (*candidates)[256],
                              int count, size_t word_start, size_t cursor) {
  Tab *t = &ed->tabs[ed->active_tab];
  int e_rows, e_cols; (void)e_cols;
  getmaxyx(ed->w_editor, e_rows, e_cols);
  int dr = t->cy - t->scroll_y;
  int dc = t->cx - t->scroll_x;
  if (dr < 0 || dr >= e_rows) return false;

  // Popup dimensions
  int visible = count < 10 ? count : 10;
  int pw = 26;
  for (int i = 0; i < count; i++) {
    int l = (int)strlen(candidates[i]) + 3;
    if (l > pw) pw = l < 55 ? l : 55;
  }
  int ph = visible + 2;

  // Position
  int wy = 1 + dr + 1;
  int wx = SIDEBAR_WIDTH + 1 + dc;
  if (wy + ph > ed->rows - 1) wy = 1 + dr - ph;
  if (wy < 1) wy = 1;
  if (wx + pw > ed->cols) wx = ed->cols - pw;
  if (wx < SIDEBAR_WIDTH + 1) wx = SIDEBAR_WIDTH + 1;

  curs_set(0);
  WINDOW *wp = newwin(ph, pw, wy, wx);
  if (!wp) { curs_set(2); return false; }
  keypad(wp, TRUE);

  // Box
  for (int y = 0; y < ph; y++)
    for (int x = 0; x < pw; x++)
      mvwaddch(wp, y, x, ' ');
  wattron(wp, A_REVERSE);
  for (int x = 0; x < pw; x++) {
    mvwaddch(wp, 0, x, ' ');
    mvwaddch(wp, ph - 1, x, ' ');
  }
  wattroff(wp, A_REVERSE);
  wattron(wp, A_BOLD);
  mvwprintw(wp, 0, 1, "\xe2\x96\xb6 Complete (%d)", count);
  wattroff(wp, A_BOLD);

  int sel = 0, scroll = 0, key;

  // Draw items (fill, selected row gets full highlight)
  for (int i = 1; i < ph - 1; i++)
    for (int x = 1; x < pw - 1; x++)
      mvwaddch(wp, i, x, ' ');
  for (int i = 0; i < visible; i++) {
    if (scroll + i >= count) break;
    int idx = scroll + i;
    if (idx == sel) {
      wattron(wp, A_REVERSE);
      for (int x = 1; x < pw - 1; x++)
        mvwaddch(wp, i + 1, x, ' ');
    }
    mvwprintw(wp, i + 1, 2, "%s", candidates[idx]);
    if (idx == sel) wattroff(wp, A_REVERSE);
  }
  wnoutrefresh(wp);
  doupdate();

  while (true) {
    key = wgetch(wp);
    bool done = false;
    switch (key) {
      case KEY_UP:
        if (sel > 0) sel--; else sel = count - 1;
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + visible) scroll = sel - visible + 1;
        break;
      case KEY_DOWN:
        if (sel < count - 1) sel++; else sel = 0;
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + visible) scroll = sel - visible + 1;
        break;
      case '\n': case KEY_ENTER: case '\t':
        done = true;
        break;
      case 27: // ESC
        delwin(wp); curs_set(2); return false;
    }
    if (done) break;
    // Redraw items (full-width selection highlight)
    for (int i = 1; i < ph - 1; i++)
      for (int x = 1; x < pw - 1; x++)
        mvwaddch(wp, i, x, ' ');
    for (int i = 0; i < visible; i++) {
      if (scroll + i >= count) break;
      int idx = scroll + i;
      if (idx == sel) {
        wattron(wp, A_REVERSE);
        for (int x = 1; x < pw - 1; x++)
          mvwaddch(wp, i + 1, x, ' ');
      }
      mvwprintw(wp, i + 1, 2, "%s", candidates[idx]);
      if (idx == sel) wattroff(wp, A_REVERSE);
    }
    wnoutrefresh(wp);
    doupdate();
  }
  delwin(wp);
  curs_set(2);
  completion_insert(ed, candidates[sel], word_start, cursor);
  return true;
}

static bool try_complete(Editor *ed) {
  Tab *t = &ed->tabs[ed->active_tab];
  if (!t->gb.buf || t->gb.size == 0) return false;
  char *text = gb_get_text(&t->gb);
  size_t cursor = gb_cursor_pos(&t->gb);
  // Find word start (scan backwards)
  size_t ws = cursor;
  while (ws > 0 && is_word_char(text[ws - 1])) ws--;
  if (ws == cursor) { free(text); return false; }
  size_t plen = cursor - ws;
  char prefix[256];
  if (plen >= 255) { free(text); return false; }
  memcpy(prefix, text + ws, plen); prefix[plen] = '\0';

  char candidates[MAX_CANDIDATES][256];
  int count = collect_candidates(text, prefix, candidates);
  free(text);

  if (count == 0) return false;
  if (count == 1) {
    completion_insert(ed, candidates[0], ws, cursor);
    return true;
  }
  return completion_popup(ed, candidates, count, ws, cursor);
}

void input_handle(Editor *ed, int ch) {
  if (ch == KEY_RESIZE) { ui_resize(ed); return; }

  // If terminal is focused, forward input to PTY
  if (ed->term_focus && ed->term_visible) {
    if (ch == 20) { // Ctrl+T toggles focus back to editor
      ed->term_focus = false;
      return;
    }
    if (ch == KEY_MOUSE) {
      MEVENT ev;
      if (getmouse(&ev) == OK) handle_mouse(ed, &ev);
      return;
    }
    // Forward to terminal
    char c;
    switch (ch) {
    case KEY_BACKSPACE: case 127: case 8: c = 0x7f; term_write(ed, &c, 1); break;
    case KEY_ENTER:      c = '\r'; term_write(ed, &c, 1); break;
    case KEY_UP:         term_write(ed, "\e[A", 3); break;
    case KEY_DOWN:       term_write(ed, "\e[B", 3); break;
    case KEY_RIGHT:      term_write(ed, "\e[C", 3); break;
    case KEY_LEFT:       term_write(ed, "\e[D", 3); break;
    case KEY_HOME:       term_write(ed, "\e[H", 3); break;
    case KEY_END:        term_write(ed, "\e[F", 3); break;
    case KEY_DC:         term_write(ed, "\e[3~", 4); break;
    case KEY_PPAGE:      term_write(ed, "\e[5~", 4); break;
    case KEY_NPAGE:      term_write(ed, "\e[6~", 4); break;
    default:
      if (ch >= 1 && ch < 127) { c = ch; term_write(ed, &c, 1); }
      else if ((unsigned)ch >= 0x80) {
        if (ch < 256) {
          c = (char)ch; term_write(ed, &c, 1);
        } else {
          unsigned char buf[5]; int len;
          if (ch < 0x800) {
            buf[0] = 0xC0 | (ch >> 6); buf[1] = 0x80 | (ch & 0x3F); len = 2;
          } else if (ch < 0x10000) {
            buf[0] = 0xE0 | (ch >> 12);
            buf[1] = 0x80 | ((ch >> 6) & 0x3F);
            buf[2] = 0x80 | (ch & 0x3F); len = 3;
          } else if (ch <= 0x10FFFF) {
            buf[0] = 0xF0 | (ch >> 18);
            buf[1] = 0x80 | ((ch >> 12) & 0x3F);
            buf[2] = 0x80 | ((ch >> 6) & 0x3F);
            buf[3] = 0x80 | (ch & 0x3F); len = 4;
          } else break;
          term_write(ed, (char *)buf, len);
        }
      }
      break;
    }
    // Read any response immediately
    term_read(ed);
    return;
  }

  // Normal mode
  if (ch == KEY_MOUSE) {
    MEVENT ev;
    if (getmouse(&ev) == OK) {
      if (ev.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED))
        handle_mouse(ed, &ev);
      else if (ev.bstate & (BUTTON4_PRESSED | BUTTON4_CLICKED)) {
        if (ev.x <= SIDEBAR_WIDTH) // sidebar or separator
          ed->sidebar_scroll--;
        else
          ed->tabs[ed->active_tab].scroll_y--;
      } else if (ev.bstate & (BUTTON5_PRESSED | BUTTON5_CLICKED)) {
        if (ev.x <= SIDEBAR_WIDTH)
          ed->sidebar_scroll++;
        else
          ed->tabs[ed->active_tab].scroll_y++;
      }
      // clamp
      if (ed->sidebar_scroll < 0) ed->sidebar_scroll = 0;
      if (ed->tabs[ed->active_tab].scroll_y < 0)
        ed->tabs[ed->active_tab].scroll_y = 0;
    }
  } else {
    handle_keyboard(ed, ch);
  }
}
