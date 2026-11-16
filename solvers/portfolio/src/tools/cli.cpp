#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "soko/map_loader.h"
#include "sokoban.h"

int main() {
  soko::MapGrids grids;
  if (!soko::ParseMapText(std::cin, &grids)) {
    std::fprintf(stderr, "empty map\n");
    return 2;
  }

  soko::RowPointers rows = soko::BuildRowPointers(&grids);

  const auto started = std::chrono::steady_clock::now();
  char* solution =
      SolveSokoban(grids.width, grids.height, rows.map.data(), rows.items.data());
  const double milliseconds = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  std::printf("%.3f\n%s", milliseconds, solution);
  std::free(solution);
  return 0;
}
