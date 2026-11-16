// Parses Sokoban map text into the two grids the solver reads.
#ifndef NATIVE_INCLUDE_SOKO_MAP_LOADER_H_
#define NATIVE_INCLUDE_SOKO_MAP_LOADER_H_

#include <fstream>
#include <istream>
#include <string>
#include <vector>

namespace soko {

// `map_rows` holds walls and goals. `items_rows` holds the player and the boxes.
struct MapGrids {
  int width = 0;
  int height = 0;
  std::vector<std::string> map_rows;
  std::vector<std::string> items_rows;
};

// Row pointers in the form the C ABI expects. They point into the grids they came from.
struct RowPointers {
  std::vector<char*> map;
  std::vector<char*> items;
};

inline bool ParseMapText(std::istream& input, MapGrids* grids) {
  std::string line;
  size_t width = 0;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() > width) width = line.size();
    grids->map_rows.push_back(line);
  }
  while (!grids->map_rows.empty() && grids->map_rows.back().empty()) grids->map_rows.pop_back();
  if (width == 0 || grids->map_rows.empty()) return false;

  grids->width = static_cast<int>(width);
  grids->height = static_cast<int>(grids->map_rows.size());
  grids->items_rows.assign(grids->height, std::string(width, ' '));

  for (int row = 0; row < grids->height; ++row) {
    std::string& cells = grids->map_rows[row];
    cells.resize(width, ' ');
    for (size_t column = 0; column < width; ++column) {
      switch (cells[column]) {
        case '@': cells[column] = ' '; grids->items_rows[row][column] = '@'; break;
        case '+': cells[column] = '.'; grids->items_rows[row][column] = '@'; break;
        case '$': cells[column] = ' '; grids->items_rows[row][column] = '$'; break;
        case '*': cells[column] = '.'; grids->items_rows[row][column] = '$'; break;
        case '#': case '.': break;
        default: cells[column] = ' '; break;
      }
    }
  }
  return true;
}

inline bool LoadMapFile(const std::string& path, MapGrids* grids) {
  std::ifstream file(path);
  return file.is_open() && ParseMapText(file, grids);
}

inline RowPointers BuildRowPointers(MapGrids* grids) {
  RowPointers pointers;
  pointers.map.resize(grids->height);
  pointers.items.resize(grids->height);
  for (int row = 0; row < grids->height; ++row) {
    pointers.map[row] = grids->map_rows[row].data();
    pointers.items[row] = grids->items_rows[row].data();
  }
  return pointers;
}

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_MAP_LOADER_H_
