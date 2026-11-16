#ifndef NATIVE_INCLUDE_SOKOBAN_H_
#define NATIVE_INCLUDE_SOKOBAN_H_

#include <atomic>

#include "soko/types.h"

extern std::atomic<u64> g_nodes_explored;
extern std::atomic<bool> g_solution_found;
extern std::atomic<int> g_winning_thread;
extern std::atomic<bool> g_pool_exhausted;

char* SolveSokoban(int width, int height, char** map, char** items);

#ifdef SOKO_TRACE
enum class TraceKind : u8 { kPop, kChild, kSolution, kPruned, kNodePruned, kPhase };

enum class TracePhase : u8 {
  kFloodFill,
  kPiCorralSearch,
  kHallCheck,
  kPushChecks,
  kStateFill,
  kPlayerFlood,
  kBoxCopy,
  kHashInsert,
  kHeuristic,
};

enum class TraceReason : u8 {
  kNone,
  kOutsideCorral,
  kDeadSquare,
  kTwoByTwo,
  kFreeze,
  kDuplicate,
  kDeadHeuristic,
  kPiCorralDeadlock,
  kHallInfeasible,
};

struct TraceEvent {
  TraceKind kind;
  TraceReason reason;
  u32 node;
  u32 parent;
  int from_row, from_column, to_row, to_column;
  u32 g;
  u32 h;
};

extern int g_trace_thread_id;
extern void (*g_trace_event)(const TraceEvent& event);
#endif  // SOKO_TRACE

#endif  // NATIVE_INCLUDE_SOKOBAN_H_
