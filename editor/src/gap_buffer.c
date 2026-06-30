#include <stdlib.h>
#include <string.h>
#include "editor.h"

#define GB_INIT 1024

GapBuffer gb_new(void) {
  GapBuffer gb;
  gb.size = GB_INIT;
  gb.buf = malloc(gb.size);
  gb.gap_start = 0;
  gb.gap_end = gb.size;
  return gb;
}

void gb_free(GapBuffer *gb) { free(gb->buf); }

static void ensure_gap(GapBuffer *gb, size_t need) {
  size_t gap_len = gb->gap_end - gb->gap_start;
  if (gap_len >= need) return;
  size_t new_size = gb->size * 2;
  while (new_size - (gb->gap_end - gb->gap_start) < need) new_size *= 2;
  char *nb = malloc(new_size);
  memcpy(nb, gb->buf, gb->gap_start);
  size_t after = gb->size - gb->gap_end;
  memcpy(nb + new_size - after, gb->buf + gb->gap_end, after);
  free(gb->buf);
  gb->buf = nb;
  gb->gap_end = new_size - after;
  gb->size = new_size;
}

void gb_insert(GapBuffer *gb, const char *s, size_t len) {
  ensure_gap(gb, len);
  memcpy(gb->buf + gb->gap_start, s, len);
  gb->gap_start += len;
}

void gb_delete_before(GapBuffer *gb, size_t count) {
  if (gb->gap_start >= count) gb->gap_start -= count;
  else gb->gap_start = 0;
}

void gb_delete_after(GapBuffer *gb, size_t count) {
  size_t after = gb->size - gb->gap_end;
  if (after < count) count = after;
  gb->gap_end += count;
}

void gb_set_cursor(GapBuffer *gb, size_t pos) {
  size_t text_len = gb_length(gb);
  if (pos > text_len) pos = text_len;

  if (pos < gb->gap_start) {
    size_t move = gb->gap_start - pos;
    memmove(gb->buf + gb->gap_end - move, gb->buf + pos, move);
    gb->gap_start = pos;
    gb->gap_end -= move;
  } else if (pos > gb->gap_start) {
    size_t move = pos - gb->gap_start;
    memmove(gb->buf + gb->gap_start, gb->buf + gb->gap_end, move);
    gb->gap_start += move;
    gb->gap_end += move;
  }
}

size_t gb_cursor_pos(const GapBuffer *gb) { return gb->gap_start; }

size_t gb_length(const GapBuffer *gb) {
  return gb->gap_start + (gb->size - gb->gap_end);
}

char *gb_get_text(GapBuffer *gb) {
  size_t before = gb->gap_start;
  size_t after = gb->size - gb->gap_end;
  char *text = malloc(before + after + 1);
  memcpy(text, gb->buf, before);
  memcpy(text + before, gb->buf + gb->gap_end, after);
  text[before + after] = '\0';
  return text;
}

int gb_line_count(GapBuffer *gb) {
  char *text = gb_get_text(gb);
  int count = 1;
  for (char *p = text; *p; p++) if (*p == '\n') count++;
  free(text);
  return count;
}

size_t gb_line_start(GapBuffer *gb, int line) {
  if (line <= 0) return 0;
  char *text = gb_get_text(gb);
  int l = 0;
  size_t pos = 0;
  for (; text[pos] && l < line; pos++) {
    if (text[pos] == '\n') l++;
  }
  free(text);
  return pos;
}

int gb_pos_to_line(GapBuffer *gb, size_t target_pos) {
  char *text = gb_get_text(gb);
  int line = 0;
  for (size_t i = 0; i < target_pos && text[i]; i++) {
    if (text[i] == '\n') line++;
  }
  free(text);
  return line;
}
