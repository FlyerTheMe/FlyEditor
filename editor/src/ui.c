#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "editor.h"

static void init_colors(void) {
  if (!has_colors()) return;
  start_color();
  use_default_colors();
  // UI pairs
  init_pair(CP_TAB_ACTIVE,   COLOR_BLACK, COLOR_CYAN);
  init_pair(CP_TAB_INACTIVE, COLOR_WHITE, COLOR_BLACK);
  init_pair(CP_SIDEBAR,      COLOR_WHITE, COLOR_BLACK);
  init_pair(CP_SIDEBAR_SEL,  COLOR_BLACK, COLOR_CYAN);
  init_pair(CP_SIDEBAR_HEADER,COLOR_WHITE, COLOR_BLACK);
  init_pair(CP_SEP,          COLOR_WHITE, -1);
  init_pair(CP_STATUS,       COLOR_WHITE, COLOR_BLUE);
  init_pair(CP_TERM,         COLOR_GREEN, -1);
  init_pair(CP_TERM_BORDER,  COLOR_WHITE, -1);
  init_pair(CP_LINENO,       COLOR_WHITE, -1);
  init_pair(CP_LINENO_CUR,   COLOR_WHITE, -1);
  init_pair(CP_CUR_LINE,     -1,          COLOR_BLACK);
  // syntax — transparent bg (-1)
  init_pair(CP_KEYWORD,  COLOR_BLUE,   -1);
  init_pair(CP_STRING,   COLOR_RED,    -1);
  init_pair(CP_COMMENT,  COLOR_GREEN,  -1);
  init_pair(CP_NUMBER,   COLOR_CYAN,   -1);
  init_pair(CP_TYPE,     COLOR_CYAN,   -1);
  init_pair(CP_PREPROC,  COLOR_MAGENTA,-1);
  init_pair(CP_FUNC,     COLOR_YELLOW, -1);
  init_pair(CP_OPERATOR, COLOR_WHITE,  -1);
  init_pair(CP_CONST,    COLOR_RED,    -1);
  init_pair(CP_MATCH,    COLOR_BLACK,  COLOR_YELLOW);
}

void ui_init(Editor *ed) {
  initscr();
  cbreak();
  noecho();
  // Disable XON/XOFF flow control so Ctrl+S / Ctrl+Q reach ncurses
  // (cbreak keeps ICRNL intact so Enter still works)
  {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_iflag &= ~(IXON | IXOFF);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
  }
  keypad(stdscr, TRUE);
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  mouseinterval(0);
  curs_set(2); // blinking block cursor
  init_colors();

  getmaxyx(stdscr, ed->rows, ed->cols);
  ed->sidebar_select = -1;
  ed->term_visible = false;
  ed->term_focus = false;
  ed->term_fd = -1;
  ed->term_pid = 0;
  ed->term_rows = ed->rows / 4;
  if (ed->term_rows < 3) ed->term_rows = 3;

  ed->w_tabs    = newwin(1, ed->cols, 0, 0);
  ed->w_sidebar = newwin(ed->rows - 2, SIDEBAR_WIDTH, 1, 0);
  ed->w_sep     = newwin(ed->rows - 2, 1, 1, SIDEBAR_WIDTH);
  ed->w_editor  = newwin(ed->rows - 2, ed->cols - SIDEBAR_WIDTH - 1, 1, SIDEBAR_WIDTH + 1);
  ed->w_term    = NULL;
  ed->w_status  = newwin(1, ed->cols, ed->rows - 1, 0);
  keypad(ed->w_editor, TRUE);
  nodelay(ed->w_editor, FALSE);
  leaveok(ed->w_status, TRUE);   // don't let status bar steal hardware cursor
}

void ui_resize(Editor *ed) {
  getmaxyx(stdscr, ed->rows, ed->cols);
  if (ed->term_rows > ed->rows - 4) ed->term_rows = ed->rows - 4;
  if (ed->term_rows < 3) ed->term_rows = 3;
  int cr = ed->rows - 2 - (ed->term_visible ? ed->term_rows : 0);
  wresize(ed->w_tabs, 1, ed->cols); mvwin(ed->w_tabs, 0, 0);
  wresize(ed->w_sidebar, cr, SIDEBAR_WIDTH); mvwin(ed->w_sidebar, 1, 0);
  wresize(ed->w_sep, cr, 1); mvwin(ed->w_sep, 1, SIDEBAR_WIDTH);
  wresize(ed->w_editor, cr, ed->cols - SIDEBAR_WIDTH - 1); mvwin(ed->w_editor, 1, SIDEBAR_WIDTH + 1);
  if (ed->term_visible && ed->w_term) {
    wresize(ed->w_term, ed->term_rows, ed->cols - SIDEBAR_WIDTH - 1);
    mvwin(ed->w_term, 1 + cr, SIDEBAR_WIDTH + 1);
  }
  wresize(ed->w_status, 1, ed->cols); mvwin(ed->w_status, ed->rows - 1, 0);
  if (ed->term_visible) term_resize(ed);
}

/* ── Tab bar ─────────────────────────────────────────────── */
static void draw_tabs(Editor *ed) {
  werase(ed->w_tabs);
  wbkgd(ed->w_tabs, COLOR_PAIR(CP_TAB_INACTIVE));
  int x = 0;
  for (int i = 0; i < ed->tab_count && x + TAB_WIDTH <= ed->cols; i++) {
    Tab *t = &ed->tabs[i];
    const char *fn = t->filename ? t->filename : "Untitled";
    // layout: " " [dirty] name... " × "
    char buf[TAB_WIDTH + 4];
    int pos = 0;
    buf[pos++] = ' ';
    if (t->dirty) { buf[pos++] = '\xe2'; buf[pos++] = '\x97'; buf[pos++] = '\x8f'; }
    else buf[pos++] = ' ';
    int namew = TAB_WIDTH - 6; // space for " × " at end
    int k;
    for (k = 0; fn[k] && k < namew; k++) buf[pos++] = fn[k];
    while (k++ < namew) buf[pos++] = ' ';
    buf[pos++] = ' ';
    buf[pos++] = '\xc3';
    buf[pos++] = '\x97'; // ×
    buf[pos++] = ' ';
    buf[pos] = '\0';

    if (i == ed->active_tab) {
      wattron(ed->w_tabs, COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
      mvwprintw(ed->w_tabs, 0, x, "%s", buf);
      wattroff(ed->w_tabs, COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
    } else {
      wattron(ed->w_tabs, COLOR_PAIR(CP_TAB_INACTIVE) | A_DIM);
      mvwprintw(ed->w_tabs, 0, x, "%s", buf);
      wattroff(ed->w_tabs, COLOR_PAIR(CP_TAB_INACTIVE) | A_DIM);
    }
    // Tab separator
    if (i < ed->tab_count - 1 && x + TAB_WIDTH < ed->cols) {
      wattron(ed->w_tabs, A_DIM);
      mvwaddstr(ed->w_tabs, 0, x + TAB_WIDTH - 1, "\xe2\x94\x82");
      wattroff(ed->w_tabs, A_DIM);
    }
    x += TAB_WIDTH;
  }
  while (x < ed->cols) { mvwaddch(ed->w_tabs, 0, x, ' '); x++; }
  wnoutrefresh(ed->w_tabs);
}

/* ── Sidebar with file-type icons ────────────────────────── */
static const char *file_icon_for(const char *name) {
  const char *ext = strrchr(name, '.');
  if (!ext) return "\xe2\x9a\x99 "; // gear for no ext
  ext++;
  if (!strcmp(ext, "c") || !strcmp(ext, "h"))   return "\xe2\x9a\x99 ";
  if (!strcmp(ext, "cpp") || !strcmp(ext, "hpp")) return "\xe2\x9a\x99 ";
  if (!strcmp(ext, "py"))   return "\xe2\x9c\xa8 ";
  if (!strcmp(ext, "js") || !strcmp(ext, "ts") || !strcmp(ext, "jsx") || !strcmp(ext, "tsx"))
    return "\xe2\x9a\x9b ";
  if (!strcmp(ext, "go"))   return "\xe2\x9a\x99 ";
  if (!strcmp(ext, "rs"))   return "\xe2\x9a\x99 ";
  if (!strcmp(ext, "sh"))   return "\xf0\x9f\x93\x9c ";
  if (!strcmp(ext, "json")) return "{ }";
  if (!strcmp(ext, "html") || !strcmp(ext, "xml")) return "\xe2\x80\xb9\xe2\x80\xba";
  if (!strcmp(ext, "md"))   return "\xf0\x9f\x93\x9d ";
  if (!strcmp(ext, "txt"))  return "\xf0\x9f\x93\x84 ";
  if (!strcmp(ext, "png") || !strcmp(ext, "jpg") || !strcmp(ext, "svg"))
    return "\xf0\x9f\x96\xbc ";
  return "\xf0\x9f\x93\x84 ";
}

static void draw_sidebar(Editor *ed) {
  werase(ed->w_sidebar);
  wbkgd(ed->w_sidebar, COLOR_PAIR(CP_SIDEBAR));
  wattron(ed->w_sidebar, A_BOLD | COLOR_PAIR(CP_SIDEBAR_HEADER));
  mvwprintw(ed->w_sidebar, 0, 1, "EXPLORER");
  wattroff(ed->w_sidebar, A_BOLD | COLOR_PAIR(CP_SIDEBAR_HEADER));
  // Header underline
  wattron(ed->w_sidebar, A_DIM);
  for (int i = 0; i < SIDEBAR_WIDTH; i++)
    mvwaddstr(ed->w_sidebar, 1, i, "\xe2\x94\x80"); // ─
  wattroff(ed->w_sidebar, A_DIM);

  int max_rows, unused;
  getmaxyx(ed->w_sidebar, max_rows, unused);
  (void)unused;
  int header_rows = 2; // "EXPLORER" + underline
  int entry_rows = max_rows - header_rows;
  int w = SIDEBAR_WIDTH;

  for (int i = 0; i < entry_rows; i++) {
    int fi = i + ed->sidebar_scroll;
    if (fi >= ed->sidebar_count) break;
    SidebarEntry *e = &ed->sidebar_entries[fi];
    bool sel = (fi == ed->sidebar_select);
    int depth = e->depth;
    if (depth > 6) depth = 6;

    wattron(ed->w_sidebar, COLOR_PAIR(sel ? CP_SIDEBAR_SEL : CP_SIDEBAR));
    wmove(ed->w_sidebar, i + header_rows, 0);

    // indent
    for (int d = 0; d < depth; d++)
      waddstr(ed->w_sidebar, "  ");

    // expand/collapse triangle for dirs
    if (e->is_dir) {
      bool expanded = false;
      for (int x = 0; x < ed->expanded_count; x++)
        if (!strcmp(ed->expanded_dirs[x], e->path)) { expanded = true; break; }
      waddstr(ed->w_sidebar, expanded ? "\xe2\x96\xbe " : "\xe2\x96\xb8 "); // ▾ / ▸
    } else {
      waddstr(ed->w_sidebar, file_icon_for(e->name));
    }
    // name (truncate to fit)
    int remain = w - getcurx(ed->w_sidebar) - 1;
    int k;
    for (k = 0; e->name[k] && k < remain; k++)
      waddch(ed->w_sidebar, (unsigned char)e->name[k]);
    while (k++ < remain) waddch(ed->w_sidebar, ' ');

    wattroff(ed->w_sidebar, COLOR_PAIR(sel ? CP_SIDEBAR_SEL : CP_SIDEBAR));
  }
  if (ed->sidebar_scroll > 0)
    mvwaddstr(ed->w_sidebar, header_rows, w - 2, "\xe2\x96\xb2");
  if (ed->sidebar_scroll + entry_rows < ed->sidebar_count)
    mvwaddstr(ed->w_sidebar, max_rows - 1, w - 2, "\xe2\x96\xbc");
  wnoutrefresh(ed->w_sidebar);
}

/* ── Separator ───────────────────────────────────────────── */
static void draw_sep(Editor *ed) {
  werase(ed->w_sep);
  wbkgd(ed->w_sep, COLOR_PAIR(CP_SEP));
  int rows, unused2;
  getmaxyx(ed->w_sep, rows, unused2);
  (void)unused2;
  wattron(ed->w_sep, A_DIM | COLOR_PAIR(CP_SEP));
  for (int i = 0; i < rows; i++)
    mvwaddstr(ed->w_sep, i, 0, "\xe2\x94\x82"); // │
  wattroff(ed->w_sep, A_DIM | COLOR_PAIR(CP_SEP));
  wnoutrefresh(ed->w_sep);
}

/* ── Bracket matching ────────────────────────────────────── */
static int match_bracket(const char *text, size_t cursor, size_t len,
                          size_t *mpos) {
  if (cursor >= len) return 0;
  char open = text[cursor];
  char close;
  int dir; // 1=forward, -1=backward
  switch (open) {
    case '(': close = ')'; dir = 1; break;
    case ')': close = '('; dir = -1; break;
    case '[': close = ']'; dir = 1; break;
    case ']': close = '['; dir = -1; break;
    case '{': close = '}'; dir = 1; break;
    case '}': close = '{'; dir = -1; break;
    default: return 0;
  }
  int depth = 0;
  ssize_t i = cursor;
  while (i >= 0 && i < (ssize_t)len) {
    if (text[i] == open) depth++;
    else if (text[i] == close) depth--;
    if (depth == 0) { *mpos = i; return 1; }
    i += dir;
  }
  return 0;
}

/* ── Editor with syntax highlighting ─────────────────────── */
static void draw_editor(Editor *ed) {
  Tab *t = &ed->tabs[ed->active_tab];
  int e_rows, e_cols;
  getmaxyx(ed->w_editor, e_rows, e_cols);
  werase(ed->w_editor);

  char *text = gb_get_text(&t->gb);
  size_t text_len = strlen(text);
  size_t cursor_pos = gb_cursor_pos(&t->gb);

  // Find matching bracket
  size_t match_pos = 0;
  int has_match = 0;
  if (cursor_pos < text_len)
    has_match = match_bracket(text, cursor_pos, text_len, &match_pos);
  if (!has_match && cursor_pos > 0)
    has_match = match_bracket(text, cursor_pos - 1, text_len, &match_pos);

  // Find cursor line / column
  int cursor_line = 0;
  size_t cur_in_line = cursor_pos;
  for (size_t i = 0; i < cursor_pos && text[i]; i++) {
    if (text[i] == '\n') { cursor_line++; cur_in_line = cursor_pos - i - 1; }
  }
  // Also track match_pos line
  int match_line = -1;
  if (has_match && match_pos != cursor_pos) {
    match_line = 0;
    for (size_t i = 0; i < match_pos && text[i]; i++)
      if (text[i] == '\n') match_line++;
  }

  // Auto-scroll: keep cursor visible when it moves below viewport
  // (don't auto-scroll UP — that would override user mouse scrolling)
  if (cursor_line >= t->scroll_y + e_rows) t->scroll_y = cursor_line - e_rows + 1;
  if (t->scroll_y < 0) t->scroll_y = 0;

  int gutter = GUTTER_WIDTH;
  int screen_row = 0, line_no = 0;
  int hl_state = 0;

  for (size_t i = 0; screen_row < e_rows; i++) {
    if (line_no < t->scroll_y) {
      if (text[i] == '\n') line_no++;
      continue;
    }
    if (i >= text_len) break;

    size_t line_start = i;
    while (text[i] && text[i] != '\n') i++;
    size_t line_len = i - line_start;
    bool is_cursor_line = (line_no == cursor_line);

    // Current line highlight
    if (is_cursor_line) {
      wattron(ed->w_editor, COLOR_PAIR(CP_CUR_LINE));
      for (int j = 0; j < e_cols; j++)
        mvwaddch(ed->w_editor, screen_row, j, ' ');
      wattroff(ed->w_editor, COLOR_PAIR(CP_CUR_LINE));
    }

    // Line number
    int ln_pair = is_cursor_line ? CP_LINENO_CUR : CP_LINENO;
    int ln_attr = is_cursor_line ? A_BOLD : A_DIM;
    wattron(ed->w_editor, COLOR_PAIR(ln_pair) | ln_attr);
    mvwprintw(ed->w_editor, screen_row, 0, "%4d ", line_no + 1);
    wattroff(ed->w_editor, COLOR_PAIR(ln_pair) | ln_attr);
    // Gutter separator
    wattron(ed->w_editor, A_DIM | COLOR_PAIR(CP_LINENO));
    mvwaddstr(ed->w_editor, screen_row, 5, "\xe2\x94\x82"); // │
    wattroff(ed->w_editor, A_DIM | COLOR_PAIR(CP_LINENO));

    // Draw highlighted line text
    int text_x = gutter;
    int max_text_cols = e_cols - text_x;
    if (max_text_cols <= 0) { screen_row++; line_no++; continue; }

    // Extract this line
    char line_buf[8192];
    size_t cl = line_len;
    if (cl > sizeof(line_buf) - 1) cl = sizeof(line_buf) - 1;
    memcpy(line_buf, text + line_start, cl);
    line_buf[cl] = '\0';

    // Run syntax highlighter
    HLSeg segs[128];
    int n_segs = highlight_line(line_buf, t->lang, &hl_state, segs, 128);

    // Draw segments (simple: no horizontal scroll for now - we handle it later)
    // First, calculate scroll_byte: byte offset for scroll_x visual cols
    size_t scroll_byte = 0;
    int svcol = 0;
    while (svcol < t->scroll_x && scroll_byte < cl) {
      svcol++;
      unsigned char c = line_buf[scroll_byte];
      if (c < 0x80) scroll_byte += 1;
      else if ((c & 0xE0) == 0xC0) scroll_byte += 2;
      else if ((c & 0xF0) == 0xE0) scroll_byte += 3;
      else scroll_byte += 4;
    }

    if (n_segs == 0) {
      // No highlighting, draw plain
      const char *start = line_buf + scroll_byte;
      int rem = cl - scroll_byte;
      if (rem > 0) {
        char tmp[8192];
        int tc = rem;
        if (tc > (int)sizeof(tmp) - 1) tc = sizeof(tmp) - 1;
        memcpy(tmp, start, tc); tmp[tc] = '\0';
        if (is_cursor_line) wattron(ed->w_editor, COLOR_PAIR(CP_CUR_LINE));
        mvwaddstr(ed->w_editor, screen_row, text_x, tmp);
        if (is_cursor_line) wattroff(ed->w_editor, COLOR_PAIR(CP_CUR_LINE));
      }
    } else {
      int cur_x = text_x;
      for (int s = 0; s < n_segs && cur_x < e_cols; s++) {
        HLSeg *sg = &segs[s];
        // skip segments before scroll
        int seg_end = sg->start + sg->len;
        if (seg_end <= (int)scroll_byte) continue;
        int eff_start = sg->start;
        int eff_len = sg->len;
        if (eff_start < (int)scroll_byte) {
          eff_len -= (scroll_byte - eff_start);
          eff_start = scroll_byte;
        }
        if (eff_len <= 0) continue;

        // extract segment text
        char tmp[4096];
        int tc = eff_len;
        if (tc > (int)sizeof(tmp) - 1) tc = sizeof(tmp) - 1;
        memcpy(tmp, line_buf + eff_start, tc);
        tmp[tc] = '\0';

        int pair = sg->pair;
        // check for bracket match highlight override
        if (has_match) {
          size_t seg_abs_start = line_start + sg->start;
          size_t seg_abs_end = seg_abs_start + sg->len;
          if (cursor_pos >= seg_abs_start && cursor_pos < seg_abs_end)
            pair = CP_MATCH;
          else if (match_pos >= seg_abs_start && match_pos < seg_abs_end)
            pair = CP_MATCH;
        }

        if (pair > 0) wattron(ed->w_editor, COLOR_PAIR(pair));
        if (is_cursor_line && pair <= 0)
          wattron(ed->w_editor, COLOR_PAIR(CP_CUR_LINE));
        mvwaddstr(ed->w_editor, screen_row, cur_x, tmp);
        if (pair > 0) wattroff(ed->w_editor, COLOR_PAIR(pair));
        if (is_cursor_line && pair <= 0)
          wattroff(ed->w_editor, COLOR_PAIR(CP_CUR_LINE));

        // advance visual columns
        for (int k = 0; k < eff_len; ) {
          unsigned char c = line_buf[eff_start + k];
          if (c < 0x80) k += 1;
          else if ((c & 0xE0) == 0xC0) k += 2;
          else if ((c & 0xF0) == 0xE0) k += 3;
          else k += 4;
          cur_x++;
        }
      }
      // fill rest of current-line background
      if (is_cursor_line && cur_x < e_cols) {
        wattron(ed->w_editor, COLOR_PAIR(CP_CUR_LINE));
        while (cur_x < e_cols) { mvwaddch(ed->w_editor, screen_row, cur_x, ' '); cur_x++; }
        wattroff(ed->w_editor, COLOR_PAIR(CP_CUR_LINE));
      }
    }

    // Draw cursor
    if (is_cursor_line) {
      int ccol = 0;
      size_t ci = 0;
      while (ci < cur_in_line && ci < line_len) {
        ccol++;
        unsigned char c = line_buf[ci];
        if (c < 0x80) ci += 1;
        else if ((c & 0xE0) == 0xC0) ci += 2;
        else if ((c & 0xF0) == 0xE0) ci += 3;
        else ci += 4;
      }
      int csx = ccol - t->scroll_x;
      if (csx >= 0 && csx < e_cols && !ed->term_focus)
        wmove(ed->w_editor, screen_row, text_x + csx);
    }

    screen_row++;
    line_no++;
    if (i >= text_len) break;
  }
  free(text);
  wnoutrefresh(ed->w_editor);
}

/* ── Terminal panel ──────────────────────────────────────── */
static void strip_ansi(char *dst, const char *src, int max_len) {
  int di = 0;
  for (int si = 0; src[si] && di < max_len - 1; si++) {
    if (src[si] == '\e' && src[si+1] == '[') {
      si += 2;
      while (src[si] && !((src[si] >= 'A' && src[si] <= 'Z') ||
                          (src[si] >= 'a' && src[si] <= 'z')))
        si++;
      continue;
    }
    if (src[si] == '\r') continue;
    dst[di++] = src[si];
  }
  dst[di] = '\0';
}

static void draw_term(Editor *ed) {
  if (!ed->w_term) return;
  int t_rows, t_cols;
  getmaxyx(ed->w_term, t_rows, t_cols);
  werase(ed->w_term);
  wbkgd(ed->w_term, COLOR_PAIR(CP_TERM));
  wattron(ed->w_term, COLOR_PAIR(CP_TERM_BORDER) | A_BOLD);
  mvwprintw(ed->w_term, 0, 0, " TERMINAL  Ctrl+T toggle");
  wattroff(ed->w_term, COLOR_PAIR(CP_TERM_BORDER) | A_BOLD);
  if (ed->term_total == 0 || !ed->term_buf) { wnoutrefresh(ed->w_term); return; }

  int total = ed->term_total;
  if (total > TERM_BUF_SIZE - 1) total = TERM_BUF_SIZE - 1;

  // find line boundaries by scanning ring buffer
  int line_starts[256], lc = 0;
  line_starts[lc] = 0;
  for (int i = 0; i < total && lc < 255; i++) {
    if (ed->term_buf[(ed->term_head + i) % TERM_BUF_SIZE] == '\n') {
      lc++;
      line_starts[lc] = i + 1;
    }
  }
  lc++;
  int first = lc - (t_rows - 1);
  if (first < 0) first = 0;
  for (int l = first; l < lc; l++) {
    int row = 1 + l - first;
    if (row >= t_rows) break;
    int start = line_starts[l];
    int end = (l + 1 < lc) ? line_starts[l + 1] - 1 : total;
    if (end > total) end = total;
    int len = end - start;
    if (len > 8191) len = 8191;
    char line[8192];
    for (int k = 0; k < len; k++)
      line[k] = ed->term_buf[(ed->term_head + start + k) % TERM_BUF_SIZE];
    line[len] = '\0';
    if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
    strip_ansi(line, line, sizeof(line));
    mvwprintw(ed->w_term, row, 0, "%s", line);
    int cur = getcurx(ed->w_term);
    while (cur < t_cols) { waddch(ed->w_term, ' '); cur++; }
    wattroff(ed->w_term, COLOR_PAIR(CP_TERM));
  }
  if (ed->term_focus) {
    wattron(ed->w_term, A_REVERSE);
    mvwaddch(ed->w_term, t_rows - 1, t_cols - 1, ' ');
    wattroff(ed->w_term, A_REVERSE);
  }
  wnoutrefresh(ed->w_term);
}

/* ── Status bar ──────────────────────────────────────────── */
static void draw_status(Editor *ed) {
  werase(ed->w_status);
  wbkgd(ed->w_status, COLOR_PAIR(CP_STATUS));
  Tab *t = &ed->tabs[ed->active_tab];
  char *text = gb_get_text(&t->gb);
  int cline = 0, ccol = 0;
  size_t cpos = gb_cursor_pos(&t->gb);
  for (size_t i = 0; i < cpos && text[i]; i++) {
    ccol++;
    if (text[i] == '\n') { cline++; ccol = 0; }
  }
  free(text);

  const char *fn = t->filename ? t->filename : "Untitled";
  const char *dirty = t->dirty ? " \xe2\x97\x8f" : "";
  const char *mode = ed->term_focus ? "TERM" : "NORMAL";
  static const char *lang_names[] = {
    "", "C", "Go", "Rust", "Python", "JS/TS", "Shell", "Makefile", "JSON", "HTML", "Markdown"
  };
  const char *lang_name = (t->lang >= 0 && t->lang <= LANG_MD) ? lang_names[t->lang] : "";

  int x = 1;
  mvwprintw(ed->w_status, 0, 0, " %s%s", fn, dirty);
  x = getcurx(ed->w_status);
  if (lang_name[0]) {
    mvwprintw(ed->w_status, 0, x, " \xe2\x94\x82 %s", lang_name);
    x = getcurx(ed->w_status);
  }
  char right[64];
  snprintf(right, sizeof(right), " %s \xe2\x94\x82 Ln %d, Col %d  ", mode, cline + 1, ccol + 1);
  int rx = ed->cols - (int)strlen(right);
  if (rx > x) {
    mvwprintw(ed->w_status, 0, rx, "%s", right);
    for (int i = x; i < rx; i++)
      mvwaddch(ed->w_status, 0, i, ' ');
  }
  wnoutrefresh(ed->w_status);
}

/* ── Full render ─────────────────────────────────────────── */
void ui_render(Editor *ed) {
  draw_tabs(ed);
  draw_sep(ed);
  draw_sidebar(ed);
  draw_editor(ed);
  if (ed->term_visible) draw_term(ed);
  draw_status(ed);

  // Restore editor cursor so doupdate() puts hardware cursor in the text area
  Tab *t = &ed->tabs[ed->active_tab];
  if (t->gb.buf && !ed->term_focus) {
    int e_rows, e_cols;
    getmaxyx(ed->w_editor, e_rows, e_cols);
    int display_row = t->cy - t->scroll_y;
    int display_col = t->cx - t->scroll_x + GUTTER_WIDTH; // gutter offset
    if (display_row >= 0 && display_row < e_rows && display_col >= 0 && display_col < e_cols) {
      wmove(ed->w_editor, display_row, display_col);
      wnoutrefresh(ed->w_editor);
    }
  }

  doupdate();
}
