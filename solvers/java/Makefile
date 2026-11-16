# Java. `make` compiles to build/classes and writes a launcher in bin/.
BIN_DIR = bin
OBJ_DIR = build
SOLVE   = $(BIN_DIR)/sokosolve
SOURCES = $(wildcard src/*.java src/*/*.java)

.PHONY: solve clean

solve: $(SOLVE)

$(OBJ_DIR)/classes.stamp: $(SOURCES) | $(OBJ_DIR)
	javac -d $(OBJ_DIR)/classes $(SOURCES)
	touch $@

$(SOLVE): $(OBJ_DIR)/classes.stamp | $(BIN_DIR)
	printf '#!/bin/sh\nhere="$$(cd "$$(dirname "$$0")/.." && pwd)"\nexec java -Xmx8g -XX:+UseG1GC -cp "$$here/$(OBJ_DIR)/classes" Main\n' > $@
	chmod +x $@

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
