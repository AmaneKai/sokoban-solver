# Eight-thread C portfolio from the first version of the project.
ifeq ($(OS),Windows_NT)
    EXE = .exe
    RM_DIR = cmd /c rmdir /S /Q
else
    EXE =
    RM_DIR = rm -rf
endif

CC       = gcc
CFLAGS   = -O3 -march=native -Wall -Wextra -Iinclude -pthread
BIN_DIR  = bin
OBJ_DIR  = build
SOLVE    = $(BIN_DIR)/sokosolve$(EXE)
OBJECTS  = $(OBJ_DIR)/solver.o $(OBJ_DIR)/main.c.o

.PHONY: solve clean

solve: $(SOLVE)

$(SOLVE): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

$(OBJ_DIR)/solver.o: src/solver.c include/solver.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/main.c.o: src/main.c include/solver.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	-$(RM_DIR) $(OBJ_DIR) $(BIN_DIR)
