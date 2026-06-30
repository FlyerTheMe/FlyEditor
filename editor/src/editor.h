#pragma once
#include <ncurses.h>
#include <stdbool.h>
#include <sys/select.h>

#define MAX_TABS 10
#define MAX_PATH 256
#define SB_PATH 128
#define SIDEBAR_WIDTH 24
#define TAB_WIDTH 16
#define TERM_BUF_SIZE (128 * 1024)
#define MAX_SIDEBAR 512
#define GUTTER_WIDTH 6

typedef struct {
  char *buf;
  size_t size;
  size_t gap_start;
  size_t gap_end;
} GapBuffer;

typedef enum {
  LANG_NONE, LANG_C, LANG_GO, LANG_RUST, LANG_PYTHON,
  LANG_JS, LANG_SHELL, LANG_MAKE, LANG_JSON, LANG_HTML, LANG_MD
} Lang;

typedef struct {
  GapBuffer gb;
  char filepath[MAX_PATH];
  char *filename;
  Lang lang;
  bool dirty;
  int cx, cy;
  int scroll_x, scroll_y;
} Tab;

typedef struct {
  char name[MAX_PATH];
  char path[SB_PATH];    // shorter: just relative path
  bool is_dir;
  int depth;
} SidebarEntry;

typedef struct {
  Tab tabs[MAX_TABS];
  int tab_count;
  int active_tab;

  int sidebar_count;
  SidebarEntry *sidebar_entries;
  int sidebar_scroll;
  int sidebar_select;
  char expanded_dirs[64][SB_PATH];
  int expanded_count;

  bool term_visible;
  bool term_focus;
  int term_pid;
  int term_fd;
  int term_rows;
  char *term_buf;             // heap-allocated, TERM_BUF_SIZE
  int term_head;
  int term_total;
  WINDOW *w_term;

  bool running;
  int rows, cols;
  WINDOW *w_tabs;
  WINDOW *w_sidebar;
  WINDOW *w_sep;
  WINDOW *w_editor;
  WINDOW *w_status;
} Editor;

// color pair IDs
enum {
  CP_TAB_ACTIVE = 1, CP_TAB_INACTIVE, CP_SIDEBAR, CP_SIDEBAR_SEL,
  CP_SIDEBAR_HEADER, CP_SEP, CP_STATUS, CP_TERM, CP_TERM_BORDER,
  CP_LINENO, CP_LINENO_CUR, CP_CUR_LINE,
  // syntax (16+)
  CP_KEYWORD = 16, CP_STRING, CP_COMMENT, CP_NUMBER, CP_TYPE,
  CP_PREPROC, CP_FUNC, CP_OPERATOR, CP_CONST, CP_MATCH,
};

// highlight.c
typedef struct { int start, len, pair; } HLSeg;
Lang highlight_detect(const char *filename);
int highlight_line(const char *line, Lang lang, int *state, HLSeg *segs, int max);

// gap_buffer.c
GapBuffer gb_new(void);
void gb_free(GapBuffer *gb);
void gb_insert(GapBuffer *gb, const char *s, size_t len);
void gb_delete_before(GapBuffer *gb, size_t count);
void gb_delete_after(GapBuffer *gb, size_t count);
void gb_set_cursor(GapBuffer *gb, size_t pos);
size_t gb_cursor_pos(const GapBuffer *gb);
size_t gb_length(const GapBuffer *gb);
char *gb_get_text(GapBuffer *gb);
int gb_line_count(GapBuffer *gb);
size_t gb_line_start(GapBuffer *gb, int line);
int gb_pos_to_line(GapBuffer *gb, size_t pos);

// file.c
bool file_open_tab(Tab *tab, const char *path);
bool file_save_tab(Tab *tab);
void file_list_dir(Editor *ed);
void sidebar_toggle(Editor *ed, int idx);

// terminal.c
bool term_spawn(Editor *ed);
void term_write(Editor *ed, const char *data, int len);
void term_read(Editor *ed);
void term_resize(Editor *ed);
void term_kill(Editor *ed);

// ui.c
void ui_init(Editor *ed);
void ui_resize(Editor *ed);
void ui_render(Editor *ed);

// input.c
bool new_tab(Editor *ed, const char *path);
void input_handle(Editor *ed, int ch);
