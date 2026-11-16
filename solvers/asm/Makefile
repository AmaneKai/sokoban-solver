# x86-64 Linux only: the solver talks to the kernel directly and needs AVX2 and BMI1.
BIN_DIR   = bin
OBJ_DIR   = build
SOLVE     = $(BIN_DIR)/sokosolve

.PHONY: solve clean

solve: $(SOLVE)

$(OBJ_DIR)/sokobot.o: sokobot.asm | $(OBJ_DIR)
	nasm -f elf64 -o $@ $<

$(SOLVE): $(OBJ_DIR)/sokobot.o | $(BIN_DIR)
	ld -o $@ $<

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
