# Emscripten Fibers Example Makefile

# Emscripten compiler
CC = emcc

# Source files
SRC = main.c

# Output files
OUT_JS = main.js
OUT_HTML = main.html

# Compiler flags
CFLAGS = -O3 \
         -sASYNCIFY=1 \
         -sASYNCIFY_STACK_SIZE=65536 \
         -sSTACK_SIZE=5242880 \
         -sALLOW_MEMORY_GROWTH=1 \
         -Wall

# Default target - build both JS and HTML
all: $(OUT_JS) $(OUT_HTML)

# Build JS output
$(OUT_JS): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT_JS)

# Build HTML output
$(OUT_HTML): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT_HTML)

# Run with Node.js
run: $(OUT_JS)
	node $(OUT_JS)

# Clean build artifacts
clean:
	rm -f $(OUT_JS) $(OUT_HTML) main.wasm

# Rebuild everything
rebuild: clean all

.PHONY: all run clean rebuild
