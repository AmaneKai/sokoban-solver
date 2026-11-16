#include "flags.h"
#include "soko/map_loader.h"
#include "sokoban.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <string>
#include <vector>

#ifdef _WIN32
#define R ""
#define B ""
#define DIM ""
#define GRN ""
#define RED ""
#define YLW ""
#define CYN ""
#define MGT ""
#else
#define R   "\033[0m"
#define B   "\033[1m"
#define DIM "\033[2m"
#define GRN "\033[38;5;114m"
#define RED "\033[38;5;203m"
#define YLW "\033[38;5;221m"
#define CYN "\033[38;5;117m"
#define MGT "\033[38;5;183m"
#endif

struct Puzzle {
  std::string name;
  int width  = 0;
  int height = 0;
  std::vector<std::string> map_rows;
  std::vector<std::string> items_rows;
};

static bool LoadPuzzle(const std::string& path, const std::string& name,
                       Puzzle* out) {
  soko::MapGrids grids;
  if (!soko::LoadMapFile(path, &grids)) return false;

  out->name = name;
  out->width = grids.width;
  out->height = grids.height;
  out->map_rows = std::move(grids.map_rows);
  out->items_rows = std::move(grids.items_rows);
  return true;
}

enum class Status { kSolved, kFailed, kTimeout, kNoFile };

struct Result {
  std::string name;
  Status status = Status::kFailed;
  int moves = 0;
  int pushes = 0;
  bool valid = false;
  int thread = -1;
  u64 nodes = 0;
  double elapsed_ms = 0.0;
  int width = 0;
  int height = 0;
  int boxes = 0;
  int goals = 0;
  int floor = 0;
  u64 thread_nodes[kThreadSlots] = {};
  u8 thread_outcome[kThreadSlots] = {};
};

static const char* OutcomeName(u8 outcome) {
  switch (static_cast<ThreadOutcome>(outcome)) {
    case ThreadOutcome::kNotRun: return "not_run";
    case ThreadOutcome::kRunning: return "running";
    case ThreadOutcome::kSolved: return "solved";
    case ThreadOutcome::kStopped: return "stopped";
    case ThreadOutcome::kExhausted: return "exhausted";
    case ThreadOutcome::kPoolFull: return "pool_full";
    case ThreadOutcome::kHopeless: return "hopeless";
    case ThreadOutcome::kNoMemory: return "no_memory";
  }
  return "unknown";
}

// Replays a solution on the puzzle. Returns the number of pushes, or -1 if a move walks into
// a wall, pushes a box into something, or the boxes do not all end on goals.
static int ReplayPushes(const Puzzle& p, const char* moves) {
  std::vector<std::string> boxes(p.height, std::string(p.width, ' '));
  int pr = -1, pc = -1;
  for (int r = 0; r < p.height; ++r) {
    for (int c = 0; c < p.width; ++c) {
      if (p.items_rows[r][c] == '$') boxes[r][c] = '$';
      if (p.items_rows[r][c] == '@') pr = r, pc = c;
    }
  }
  auto wall = [&](int r, int c) {
    return r < 0 || c < 0 || r >= p.height || c >= p.width || p.map_rows[r][c] == '#';
  };
  int pushes = 0;
  for (const char* m = moves; *m; ++m) {
    int dr = 0, dc = 0;
    switch (*m | 0x20) {
      case 'u': dr = -1; break;
      case 'd': dr = 1; break;
      case 'l': dc = -1; break;
      case 'r': dc = 1; break;
      default: return -1;
    }
    const int nr = pr + dr, nc = pc + dc;
    if (wall(nr, nc)) return -1;
    if (boxes[nr][nc] == '$') {
      const int br = nr + dr, bc = nc + dc;
      if (wall(br, bc) || boxes[br][bc] == '$') return -1;
      boxes[nr][nc] = ' ';
      boxes[br][bc] = '$';
      ++pushes;
    }
    pr = nr, pc = nc;
  }
  for (int r = 0; r < p.height; ++r) {
    for (int c = 0; c < p.width; ++c) {
      if (boxes[r][c] == '$' && p.map_rows[r][c] != '.') return -1;
    }
  }
  return pushes;
}

// Static facts about a map, so the CSV can be analysed without re-reading the map files.
static void FillMapFacts(const Puzzle& p, Result* r) {
  r->width = p.width;
  r->height = p.height;
  for (int row = 0; row < p.height; ++row) {
    for (int col = 0; col < p.width; ++col) {
      const char cell = p.map_rows[row][col];
      const char item = p.items_rows[row][col];
      if (item == '$') ++r->boxes;
      if (cell == '.') ++r->goals;
      if (cell != '#' && cell != '\0') ++r->floor;
    }
  }
}

static int CountMoves(const char* s) {
  int n = 0;
  for (; *s; ++s) ++n;
  return n;
}

static const char* kSpinner[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
static constexpr int kSpinLen = 10;

static Result RunOne(const Puzzle& puzzle, double timeout_sec) {
  soko::MapGrids grids;
  grids.width = puzzle.width;
  grids.height = puzzle.height;
  grids.map_rows = puzzle.map_rows;
  grids.items_rows = puzzle.items_rows;
  soko::RowPointers rows = soko::BuildRowPointers(&grids);

  auto future = std::async(std::launch::async, [&]() -> char* {
    return SolveSokoban(puzzle.width, puzzle.height,
                        rows.map.data(), rows.items.data());
  });

  const auto t0 = std::chrono::high_resolution_clock::now();
  const auto deadline = t0 + std::chrono::duration<double>(timeout_sec);
  int spin_idx = 0;
  constexpr auto kSpinStep = std::chrono::milliseconds(80);
  bool timed_out = false;

  std::string display = puzzle.name;
  if (display.size() > 34) display = display.substr(0, 31) + "...";

  while (true) {
    auto status = future.wait_for(kSpinStep);
    const auto now = std::chrono::high_resolution_clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(now - t0).count();

    if (status == std::future_status::ready) break;

    printf("  %s%s%s  %s%-34s%s  %s%.1f s%s\r",
           CYN, kSpinner[spin_idx % kSpinLen], R,
           DIM, display.c_str(), R,
           DIM, elapsed / 1000.0, R);
    fflush(stdout);
    ++spin_idx;

    if (now >= deadline) {
      // Ask every thread to stop, then wait until all have flushed their counts. Threads
      // check the flag every 128 expansions, so this is quick.
      g_solution_found.store(true);
      future.wait();
      timed_out = true;
      break;
    }
  }

  const auto t1 = std::chrono::high_resolution_clock::now();

  printf("\033[2K\r");

  Result res;
  res.name = puzzle.name;
  res.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  FillMapFacts(puzzle, &res);

  char* sol = future.get();
  res.nodes = g_nodes_explored.load();
  for (int t = 0; t < kThreadSlots; ++t) {
    res.thread_nodes[t] = g_thread_nodes[t].load();
    res.thread_outcome[t] = g_thread_outcome[t].load();
  }

  if (timed_out) {
    res.status = Status::kTimeout;
    free(sol);
    return res;
  }

  const bool ok  = (sol && sol[0] != '\0');
  res.status = ok ? Status::kSolved : Status::kFailed;
  res.moves = ok ? CountMoves(sol) : 0;
  res.pushes = ok ? ReplayPushes(puzzle, sol) : 0;
  res.valid = ok && res.pushes >= 0;
  res.thread = ok ? g_winning_thread.load() : -1;
  free(sol);
  return res;
}

static void PrintHeader(int total) {
  printf("\n");
  printf("  %s sokobench%s  %s%d puzzles%s\n", B, R, DIM, total, R);
  printf("\n");
  printf("  %s%-34s  %6s  %7s  %11s  %9s  %-9s%s\n",
         DIM, "map", "thread", "moves", "nodes", "time", "status", R);
  printf("  %s%s%s\n", DIM,
         "──────────────────────────────────────────────────────────────────────────────────",
         R);
}

static void PrintResult(const Result& r) {
  const char* badge_col;
  const char* badge;
  switch (r.status) {
    case Status::kSolved:  badge_col = GRN; badge = "✔ solved "; break;
    case Status::kTimeout: badge_col = YLW; badge = "◷ timeout"; break;
    case Status::kNoFile:  badge_col = DIM; badge = "? no file"; break;
    default:               badge_col = RED; badge = "✘ failed "; break;
  }

  std::string display = r.name;
  if (display.size() > 34) display = display.substr(0, 31) + "...";

  char thread_buf[16];
  if (r.status == Status::kSolved && r.thread >= 0)
    snprintf(thread_buf, sizeof(thread_buf), "T%d", r.thread);
  else
    snprintf(thread_buf, sizeof(thread_buf), "-");

  char nodes_buf[24];
  if (r.nodes >= 1'000'000)
    snprintf(nodes_buf, sizeof(nodes_buf), "%.1fM", r.nodes / 1'000'000.0);
  else if (r.nodes >= 1'000)
    snprintf(nodes_buf, sizeof(nodes_buf), "%.1fK", r.nodes / 1'000.0);
  else
    snprintf(nodes_buf, sizeof(nodes_buf), "%llu", (unsigned long long)r.nodes);

  char time_buf[24];
  if (r.elapsed_ms >= 1000.0)
    snprintf(time_buf, sizeof(time_buf), "%.2f s", r.elapsed_ms / 1000.0);
  else
    snprintf(time_buf, sizeof(time_buf), "%.0f ms", r.elapsed_ms);

  char moves_buf[16];
  if (r.status == Status::kSolved)
    snprintf(moves_buf, sizeof(moves_buf), "%d", r.moves);
  else
    snprintf(moves_buf, sizeof(moves_buf), "-");

  printf("  %-34s  %s%6s%s  %7s  %11s  %s%9s%s  %s%s%s\n",
         display.c_str(),
         CYN, thread_buf, R,
         moves_buf,
         nodes_buf,
         DIM, time_buf, R,
         badge_col, badge, R);
}

static void PrintSummary(const std::vector<Result>& results, double timeout_sec) {
  int solved = 0, failed = 0, timeout = 0;
  double total_ms = 0.0;
  u64 total_nodes = 0;
  int total_moves = 0;
  int thread_wins[kThreadCount] = {};
  int hard_wins[kThreadCount] = {};

  for (const auto& r : results) {
    switch (r.status) {
      case Status::kSolved:
        ++solved;
        total_moves += r.moves;
        if (r.thread >= 0 && r.thread < kThreadCount) {
          ++thread_wins[r.thread];
          if (r.elapsed_ms >= 50.0) ++hard_wins[r.thread];
        }
        break;
      case Status::kTimeout: ++timeout; break;
      default: ++failed;  break;
    }
    total_ms    += r.elapsed_ms;
    total_nodes += r.nodes;
  }

  const double pct = results.empty() ? 0.0
      : 100.0 * solved / (int)results.size();

  printf("  %s%s%s\n\n", DIM,
         "──────────────────────────────────────────────────────────────────────────────────",
         R);
  printf("  %sresults%s\n\n", B, R);
  printf("  %s✔%s  solved   %s%d%s  %s(%.1f%%)%s\n",
         GRN, R, B, solved, R, DIM, pct, R);
  if (timeout > 0)
    printf("  %s◷%s  timeout  %s%d%s  %s(>%.0fs)%s\n",
           YLW, R, B, timeout, R, DIM, timeout_sec, R);
  if (failed > 0)
    printf("  %s✘%s  failed   %s%d%s\n", RED, R, B, failed, R);

  printf("\n");

  char nodes_buf[32];
  if (total_nodes >= 1'000'000'000ULL)
    snprintf(nodes_buf, sizeof(nodes_buf), "%.2fB", total_nodes / 1'000'000'000.0);
  else if (total_nodes >= 1'000'000)
    snprintf(nodes_buf, sizeof(nodes_buf), "%.2fM", total_nodes / 1'000'000.0);
  else
    snprintf(nodes_buf, sizeof(nodes_buf), "%llu", (unsigned long long)total_nodes);

  printf("  moves       %s%d%s\n",   B, total_moves, R);
  printf("  nodes       %s%s%s\n",   B, nodes_buf, R);
  printf("  time        %s%.2f s%s  %savg %.0f ms%s\n",
         B, total_ms / 1000.0, R,
         DIM, results.empty() ? 0.0 : total_ms / results.size(), R);

  printf("  wins (all)  ");
  for (int t = 0; t < kThreadCount; ++t) {
    printf("%sT%d:%s %s%-4d%s ", DIM, t, R, B, thread_wins[t], R);
  }
  printf("\n  wins >=50ms ");
  for (int t = 0; t < kThreadCount; ++t) {
    printf("%sT%d:%s %s%-4d%s ", DIM, t, R, B, hard_wins[t], R);
  }
  printf("\n\n");
}

// The CSV is written one row per map as results arrive, so an interrupted run keeps its data.
// Per-thread columns cover every configured thread; threads that did not run say not_run.
static void WriteCsvHeader(FILE* f) {
  fprintf(f, "map,status,valid,thread,moves,pushes,nodes,time_ms,width,height,boxes,goals,floor,lineup");
  for (int t = 0; t < kThreadCount; ++t) fprintf(f, ",t%d_outcome,t%d_nodes", t, t);
  fprintf(f, "\n");
}

static void WriteCsvRow(FILE* f, const Result& r, const std::string& lineup) {
  const char* status;
  switch (r.status) {
    case Status::kSolved:  status = "solved";  break;
    case Status::kTimeout: status = "timeout"; break;
    case Status::kNoFile:  status = "no_file"; break;
    default:               status = "failed";  break;
  }
  fprintf(f, "\"%s\",%s,%d,%d,%d,%d,%llu,%.1f,%d,%d,%d,%d,%d,\"%s\"", r.name.c_str(), status,
          r.valid ? 1 : 0, r.thread, r.moves, r.pushes, (unsigned long long)r.nodes, r.elapsed_ms, r.width,
          r.height, r.boxes, r.goals, r.floor, lineup.c_str());
  for (int t = 0; t < kThreadCount; ++t) {
    fprintf(f, ",%s,%llu", OutcomeName(r.thread_outcome[t]),
            (unsigned long long)r.thread_nodes[t]);
  }
  fprintf(f, "\n");
  fflush(f);
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "usage: sokobench <masterlist.txt> [maps_dir] [timeout_sec] [out.csv]\n");
    return 1;
  }

#ifdef _WIN32
  freopen("NUL", "w", stderr);
#else
  freopen("/dev/null", "w", stderr);
#endif

  const std::string list_path = argv[1];
  const std::string maps_dir = (argc >= 3) ? argv[2] : ".";
  const double timeout_sec = (argc >= 4) ? atof(argv[3]) : 15.0;
  const std::string csv_path = (argc >= 5) ? argv[4] : "";

  std::ifstream list_f(list_path);
  if (!list_f.is_open()) return 1;

  std::vector<std::string> names;
  std::string line;
  while (std::getline(list_f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) names.push_back(line);
  }
  if (names.empty()) return 1;

  PrintHeader((int)names.size());

  const char* lineup_env = std::getenv("SOKO_THREADS");
  const std::string lineup = lineup_env ? lineup_env : "default";
  FILE* csv = nullptr;
  if (!csv_path.empty()) {
    csv = fopen(csv_path.c_str(), "w");
    if (csv) WriteCsvHeader(csv);
  }

  std::vector<Result> results;
  results.reserve(names.size());

  for (int i = 0; i < (int)names.size(); ++i) {
    const std::string& name = names[i];
    const std::string  path = maps_dir + "/" + name + ".txt";

    Puzzle puzzle;
    if (!LoadPuzzle(path, name, &puzzle)) {
      Result r;
      r.name   = name;
      r.status = Status::kNoFile;
      PrintResult(r);
      if (csv) WriteCsvRow(csv, r, lineup);
      results.push_back(r);
      continue;
    }

    Result r = RunOne(puzzle, timeout_sec);
    PrintResult(r);
    if (csv) WriteCsvRow(csv, r, lineup);
    results.push_back(r);
  }

  PrintSummary(results, timeout_sec);

  if (csv) {
    fclose(csv);
    printf("  %scsv%s     %s%s%s\n\n", B, R, DIM, csv_path.c_str(), R);
  }

  return 0;
}
