# The first simplified C++ rewrite of the solver, without the Java bridge.
ifeq ($(OS),Windows_NT)
    EXE = .exe
    RM_DIR = cmd /c rmdir /S /Q
else
    EXE =
    RM_DIR = rm -rf
endif

CXX      = g++
CXXFLAGS = -O3 -Wall -Wextra -std=c++20 -Iinclude -march=native -pthread
BIN_DIR  = bin
OBJ_DIR  = build
SOLVE    = $(BIN_DIR)/sokosolve$(EXE)
OBJECTS  = $(addprefix $(OBJ_DIR)/,board.o deadlock.o heuristic.o solver.o main.o)
HEADERS  = $(wildcard include/*.h)

.PHONY: solve clean

solve: $(SOLVE)

$(SOLVE): $(OBJECTS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS)

$(OBJ_DIR)/%.o: src/%.cpp $(HEADERS) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	-$(RM_DIR) $(OBJ_DIR) $(BIN_DIR)
