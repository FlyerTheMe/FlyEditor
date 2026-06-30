#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include "editor.h"

bool file_open_tab(Tab *tab, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  char *content = malloc(sz + 1);
  size_t read = fread(content, 1, sz, f);
  content[read] = '\0';
  fclose(f);

  gb_free(&tab->gb);
  tab->gb = gb_new();
  if (read > 0) { gb_insert(&tab->gb, content, read); gb_set_cursor(&tab->gb, 0); }
  free(content);

  strncpy(tab->filepath, path, MAX_PATH - 1);
  tab->filepath[MAX_PATH - 1] = '\0';
  const char *name = strrchr(path, '/');
  tab->filename = (char *)(name ? name + 1 : tab->filepath);
  tab->dirty = false;
  tab->cx = tab->cy = 0;
  tab->scroll_x = tab->scroll_y = 0;
  tab->lang = highlight_detect(tab->filename);
  return true;
}

bool file_save_tab(Tab *tab) {
  if (!tab->filepath[0]) return false;
  FILE *f = fopen(tab->filepath, "w");
  if (!f) return false;
  char *text = gb_get_text(&tab->gb);
  fputs(text, f); free(text);
  fclose(f);
  tab->dirty = false;
  return true;
}

static int dir_filter(const struct dirent *e) { return e->d_name[0] != '.'; }

static int alphasort_c(const struct dirent **a, const struct dirent **b) {
  bool da = ((*a)->d_type == DT_DIR), db = ((*b)->d_type == DT_DIR);
  if (da && !db) return -1;
  if (!da && db) return 1;
  return strcmp((*a)->d_name, (*b)->d_name);
}

static bool is_expanded(Editor *ed, const char *path) {
  for (int i = 0; i < ed->expanded_count; i++)
    if (!strcmp(ed->expanded_dirs[i], path)) return true;
  return false;
}

static void toggle_expand(Editor *ed, const char *path) {
  for (int i = 0; i < ed->expanded_count; i++) {
    if (!strcmp(ed->expanded_dirs[i], path)) {
      // remove: shift left
      for (int j = i; j < ed->expanded_count - 1; j++)
        strcpy(ed->expanded_dirs[j], ed->expanded_dirs[j + 1]);
      ed->expanded_count--;
      return;
    }
  }
  // add
  if (ed->expanded_count < 64) {
    strncpy(ed->expanded_dirs[ed->expanded_count], path, SB_PATH - 1);
    ed->expanded_dirs[ed->expanded_count][SB_PATH - 1] = '\0';
    ed->expanded_count++;
  }
}

static void sidebar_add(Editor *ed, const char *name, const char *path,
                         bool is_dir, int depth) {
  if (ed->sidebar_count >= MAX_SIDEBAR) return;
  SidebarEntry *e = &ed->sidebar_entries[ed->sidebar_count];
  strncpy(e->name, name, MAX_PATH - 1); e->name[MAX_PATH - 1] = '\0';
  strncpy(e->path, path, SB_PATH - 1); e->path[SB_PATH - 1] = '\0';
  e->is_dir = is_dir;
  e->depth = depth;
  ed->sidebar_count++;
}

static void list_recursive(Editor *ed, const char *dirpath, int depth) {
  DIR *d = opendir(dirpath);
  if (!d) return;
  struct dirent **list;
  int n = scandir(dirpath, &list, dir_filter, alphasort_c);
  if (n < 0) { closedir(d); return; }
  for (int i = 0; i < n; i++) {
    bool is_dir = (list[i]->d_type == DT_DIR);
    if (list[i]->d_type == DT_UNKNOWN) {
      char tmp[512];
      snprintf(tmp, sizeof(tmp), "%s/%s", dirpath, list[i]->d_name);
      struct stat st;
      if (stat(tmp, &st) == 0) is_dir = S_ISDIR(st.st_mode);
    }
    char child_path[512];
    snprintf(child_path, sizeof(child_path), "%s/%s", dirpath, list[i]->d_name);
    sidebar_add(ed, list[i]->d_name, child_path, is_dir, depth);
    if (is_dir && is_expanded(ed, child_path))
      list_recursive(ed, child_path, depth + 1);
    free(list[i]);
  }
  free(list);
  closedir(d);
}

void file_list_dir(Editor *ed) {
  ed->sidebar_count = 0;
  list_recursive(ed, ".", 0);
}

void sidebar_toggle(Editor *ed, int idx) {
  if (idx < 0 || idx >= ed->sidebar_count) return;
  SidebarEntry *e = &ed->sidebar_entries[idx];
  if (!e->is_dir) return;
  toggle_expand(ed, e->path);
  ed->sidebar_select = idx;
  file_list_dir(ed);
}
