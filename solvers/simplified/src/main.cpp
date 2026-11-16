// Command line front end: a map on stdin, the search time in ms and then the moves on stdout.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "solver.h"

int main() {
  const std::string text((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());

  std::vector<std::string> rows;
  size_t begin = 0;
  while (begin <= text.size()) {
    size_t end = text.find('\n', begin);
    if (end == std::string::npos) end = text.size();
    std::string row = text.substr(begin, end - begin);
    if (!row.empty() && row.back() == '\r') row.pop_back();
    rows.push_back(std::move(row));
    begin = end + 1;
  }
  while (!rows.empty() && rows.back().find_first_not_of(" \t") == std::string::npos) rows.pop_back();

  size_t width = 0;
  for (const std::string& row : rows) width = std::max(width, row.size());
  if (width == 0) {
    std::fprintf(stderr, "simplified: empty map\n");
    return 2;
  }

  // The ground and the items are separate grids. Short rows are padded with walls.
  std::vector<std::string> ground(rows.size()), items(rows.size());
  std::vector<char*> map_rows, item_rows;
  for (size_t r = 0; r < rows.size(); ++r) {
    for (size_t c = 0; c < width; ++c) {
      const char ch = c < rows[r].size() ? rows[r][c] : '#';
      ground[r] += ch == '#' ? '#' : (ch == '.' || ch == '*' || ch == '+') ? '.' : ' ';
      items[r] += (ch == '$' || ch == '*') ? '$' : (ch == '@' || ch == '+') ? '@' : ' ';
    }
    map_rows.push_back(ground[r].data());
    item_rows.push_back(items[r].data());
  }

  const auto started = std::chrono::steady_clock::now();
  char* solution = SolveSokoban(static_cast<int>(width), static_cast<int>(rows.size()),
                                map_rows.data(), item_rows.data());
  const double milliseconds = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  std::printf("%.3f\n%s\n", milliseconds, solution ? solution : "");
  std::free(solution);
  return 0;
}
