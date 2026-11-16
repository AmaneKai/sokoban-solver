# Pure Python, managed by uv. `make` syncs the project env and creates a launcher in bin/.
BIN_DIR = bin
SOLVE   = $(BIN_DIR)/sokosolve

.PHONY: solve clean

solve: $(SOLVE)

$(SOLVE): pyproject.toml uv.lock main.py solver.py | $(BIN_DIR)
	UV_PROJECT_ENVIRONMENT=.venv uv sync --frozen --project .
	printf '#!/bin/sh\nhere="$$(cd "$$(dirname "$$0")/.." && pwd)"\nexec uv run --frozen --no-sync --project "$$here" python "$$here/main.py"\n' > $@
	chmod +x $@

$(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf .venv $(BIN_DIR)
