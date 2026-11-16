// Command line front end: a map on stdin, the search time in ms and then the moves on stdout.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "solver.h"

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(void) {
  size_t size = 0, used = 0;
  char* text = NULL;
  for (;;) {
    if (used + 4096 + 1 > size) {
      size = size ? size * 2 : 1 << 16;
      text = realloc(text, size);
      if (!text) return 2;
    }
    size_t got = fread(text + used, 1, size - used - 1, stdin);
    if (got == 0) break;
    used += got;
  }
  text[used] = '\0';

  // Split into rows, ignoring \r and trailing empty rows.
  char** rows = malloc(sizeof(char*) * (MAX_H + 1));
  int height = 0, width = 0;
  for (char* line = text; line && *line != '\0' && height <= MAX_H;) {
    char* end = strchr(line, '\n');
    size_t len = end ? (size_t)(end - line) : strlen(line);
    if (len > 0 && line[len - 1] == '\r') len--;
    rows[height++] = line;
    if (len > (size_t)width) width = (int)len;
    // Remember the row length by cutting the row at its end.
    if (end) *end = '\0';
    line[len] = '\0';
    line = end ? end + 1 : NULL;
  }
  while (height > 0 && rows[height - 1][0] == '\0') height--;
  if (width == 0 || height == 0 || width > MAX_W || height > MAX_H) {
    fprintf(stderr, "classic: map must be 1..%d by 1..%d\n", MAX_W, MAX_H);
    return 2;
  }

  // The ground and the items are separate grids. Short rows are padded with walls.
  char** map = malloc(sizeof(char*) * height);
  char** items = malloc(sizeof(char*) * height);
  int boxes = 0;
  for (int r = 0; r < height; r++) {
    map[r] = malloc(width + 1);
    items[r] = malloc(width + 1);
    size_t len = strlen(rows[r]);
    for (int c = 0; c < width; c++) {
      char ch = (size_t)c < len ? rows[r][c] : '#';
      map[r][c] = ch == '#' ? '#' : (ch == '.' || ch == '*' || ch == '+') ? '.' : ' ';
      items[r][c] = (ch == '$' || ch == '*') ? '$' : (ch == '@' || ch == '+') ? '@' : ' ';
      if (items[r][c] == '$') boxes++;
    }
    map[r][width] = items[r][width] = '\0';
  }
  if (boxes > MAX_BOXES) {
    fprintf(stderr, "classic: at most %d boxes\n", MAX_BOXES);
    return 2;
  }

  double start = now_ms();
  char* moves = solve_sokoban(width, height, map, items);
  printf("%.3f\n%s\n", now_ms() - start, moves);
  return 0;
}
