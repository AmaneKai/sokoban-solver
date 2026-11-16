ifeq ($(OS),Windows_NT)
    EXE = .exe
    RM_DIR = cmd /c rmdir /S /Q
else
    EXE =
    RM_DIR = rm -rf
endif

BIN_DIR = bin
OBJ_DIR = build
SOLVE   = $(BIN_DIR)/sokosolve$(EXE)

.PHONY: solve clean

solve: $(SOLVE)

$(SOLVE): $(wildcard src/*.rs) Cargo.toml | $(BIN_DIR)
	cargo build --release --target-dir $(OBJ_DIR)
	cp $(OBJ_DIR)/release/sokosolve$(EXE) $@

$(BIN_DIR):
	mkdir -p $@

clean:
	-$(RM_DIR) $(OBJ_DIR) $(BIN_DIR)
