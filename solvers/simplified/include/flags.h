#ifndef FLAGS_H_
#define FLAGS_H_

// Pruning
constexpr bool kUseDeadSquares = true;
constexpr bool kUseFreezeCheck = true;
constexpr bool kUsePiCorral = true;
constexpr bool kUseHallCheck = true;

// Heuristic
constexpr bool kUseReverseBfs = false;
constexpr bool kUseManhattan = true;
constexpr bool kNoHeuristic = false;
                                      
static_assert(int(kUseReverseBfs) + int(kUseManhattan) + int(kNoHeuristic) == 1,
              "flags.h: enable exactly one of kUseReverseBfs, kUseManhattan, kNoHeuristic");

constexpr bool kUseHungarian = false;

// Move generation
constexpr bool kUseMacroMoves = true;  // collapse pushes through 1-wide tunnels into one move

// Search ordering
constexpr unsigned long long kSwitchPenalty = 2'000'000;  // per box switch (1'000'000 = 1 h-point)

#endif  // FLAGS_H_
