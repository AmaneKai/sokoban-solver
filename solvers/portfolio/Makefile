ifeq ($(OS),Windows_NT)
    detected_OS := Windows
else
    detected_OS := $(shell uname -s)
endif

CXX = g++

CXXFLAGS = -O3 -Wall -Wextra \
           -I./include \
           -std=c++20 \
           -flto \
           -ffast-math \
           -funroll-loops \
           -finline-functions \
           -pthread

LFLAGS = -flto -pthread

ifeq ($(detected_OS),Darwin)
    CXX = /opt/homebrew/opt/llvm/bin/clang++

    ARCH := $(shell uname -m)
    ifeq ($(ARCH),arm64)
        ARCH_FLAGS = -mcpu=native
    else
        ARCH_FLAGS = -march=native -mtune=native
    endif

    CXXFLAGS += $(ARCH_FLAGS) \
                -O3 -ffast-math \
                -fomit-frame-pointer \
                -fvectorize -fslp-vectorize

    LFLAGS += -fno-plt -L/opt/homebrew/opt/llvm/lib -Wl,-dead_strip

    SOLVE  = sokosolve
    TRACE  = sokotrace
    BENCH  = sokobench
    RM_DIR = rm -rf
    MKDIR  = mkdir -p

else ifeq ($(detected_OS),Linux)
    CXX = g++

    CXXFLAGS += -march=native -mtune=native \
                -Ofast \
                -fomit-frame-pointer \
                -fno-plt \
                -falign-functions=32 \
                -falign-loops=32

    LFLAGS += -Wl,--gc-sections -Wl,-O1

    SOLVE  = sokosolve
    TRACE  = sokotrace
    BENCH  = sokobench
    RM_DIR = rm -rf
    MKDIR  = mkdir -p

else
    CXXFLAGS += -march=native -mtune=native

    SOLVE  = sokosolve.exe
    TRACE  = sokotrace.exe
    BENCH  = sokobench.exe
    RM_DIR = cmd /c rmdir /S /Q
    MKDIR  = cmd /c mkdir
endif

SRC_DIR   = src
TOOLS_DIR = src/tools
OBJ_DIR   = build
BIN_DIR   = bin

vpath %.cpp $(SRC_DIR) $(TOOLS_DIR)

CORE_OBJECTS   = $(OBJ_DIR)/board.o $(OBJ_DIR)/reachability.o $(OBJ_DIR)/heuristic.o \
                 $(OBJ_DIR)/deadlock.o $(OBJ_DIR)/goal_room.o
ENGINE_OBJECTS = $(CORE_OBJECTS) $(OBJ_DIR)/backward.o
SOLVE_OBJECTS  = $(ENGINE_OBJECTS) $(OBJ_DIR)/solver.o $(OBJ_DIR)/cli.o
BENCH_OBJECTS  = $(ENGINE_OBJECTS) $(OBJ_DIR)/solver.o $(OBJ_DIR)/bench.o
TRACE_OBJECTS  = $(CORE_OBJECTS) $(OBJ_DIR)/backward_trace.o $(OBJ_DIR)/solver_trace.o $(OBJ_DIR)/trace_cli.o

SOLVE := $(BIN_DIR)/$(SOLVE)
TRACE := $(BIN_DIR)/$(TRACE)
BENCH := $(BIN_DIR)/$(BENCH)
HEADERS = $(wildcard include/*.h include/soko/*.h)

all: solve trace bench

solve: $(SOLVE)
trace: $(TRACE)
bench: $(BENCH)

$(OBJ_DIR) $(BIN_DIR):
	$(MKDIR) $@

$(SOLVE): $(SOLVE_OBJECTS) | $(BIN_DIR)
	$(CXX) $(LFLAGS) -o $(SOLVE) $(SOLVE_OBJECTS)

$(BENCH): $(BENCH_OBJECTS) | $(BIN_DIR)
	$(CXX) $(LFLAGS) -o $(BENCH) $(BENCH_OBJECTS)

$(TRACE): $(TRACE_OBJECTS) | $(BIN_DIR)
	$(CXX) $(LFLAGS) -o $(TRACE) $(TRACE_OBJECTS)

$(OBJ_DIR)/%.o: %.cpp $(HEADERS) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/solver_trace.o: solver.cpp $(HEADERS) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -DSOKO_TRACE -c $< -o $@

$(OBJ_DIR)/backward_trace.o: backward.cpp $(HEADERS) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -DSOKO_TRACE -c $< -o $@

$(OBJ_DIR)/trace_cli.o: trace_cli.cpp $(HEADERS) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -DSOKO_TRACE -c $< -o $@

clean:
ifeq ($(detected_OS),Windows)
	-$(RM_DIR) $(OBJ_DIR) $(BIN_DIR) 2>nul
else
	-$(RM_DIR) $(OBJ_DIR) $(BIN_DIR)
endif

rebuild: clean all

.PHONY: all solve trace bench clean rebuild
