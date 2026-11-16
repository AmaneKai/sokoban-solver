#ifndef SOLVER_H_
#define SOLVER_H_

#include <atomic>
#include <cstdint>

extern std::atomic<uint64_t> g_nodes_explored;
extern std::atomic<bool> g_solution_found;

char* SolveSokoban(int width, int height, char** map, char** items);

#endif  // SOLVER_H_
