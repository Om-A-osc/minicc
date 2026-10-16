# MiniCC build.
#
#   make            build build/minicc
#   make test       build and run the test suite
#   make clean      remove build artifacts
#
# Requires flex, bison and a C11 compiler. On Windows use the MSYS2 shell
# (pacman -S flex bison make mingw-w64-x86_64-gcc).

CC      ?= cc
FLEX    ?= flex
BISON   ?= bison
CFLAGS  ?= -O2 -g
LDFLAGS ?=

WARN     = -std=c11 -Wall -Wextra -Wno-unused-parameter
BUILD    = build
SRCS     = $(wildcard src/*.c)
OBJS     = $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS)) $(BUILD)/parser.o $(BUILD)/lexer.o
DEPS     = $(OBJS:.o=.d)

ifeq ($(OS),Windows_NT)
  EXE = .exe
else
  EXE =
endif
BIN = $(BUILD)/minicc$(EXE)

# Absolute path of the runtime sources, baked into the driver so `minicc x.dcf -o x`
# can link the runtime from any working directory. Converted to a native Windows
# path under MSYS2 so the MinGW compiler can open it.
RUNTIME_DIR = $(shell cygpath -m "$(abspath runtime)" 2>/dev/null || echo "$(abspath runtime)")

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/parser.c $(BUILD)/parser.h: src/parser.y | $(BUILD)
	$(BISON) -d -Wall -Wcounterexamples -o $(BUILD)/parser.c src/parser.y

$(BUILD)/lexer.c: src/lexer.l $(BUILD)/parser.h | $(BUILD)
	$(FLEX) -o $@ $<

$(BUILD)/%.o: src/%.c $(BUILD)/parser.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -MMD -MP -Isrc -I$(BUILD) \
	    -DMINICC_RUNTIME_DIR='"$(RUNTIME_DIR)"' -c -o $@ $<

$(BUILD)/parser.o: $(BUILD)/parser.c
	$(CC) $(CFLAGS) -std=c11 -MMD -MP -Isrc -I$(BUILD) -c -o $@ $<

$(BUILD)/lexer.o: $(BUILD)/lexer.c
	$(CC) $(CFLAGS) -std=c11 -MMD -MP -Isrc -I$(BUILD) -c -o $@ $<

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

test: $(BIN)
	bash tests/run_tests.sh

clean:
	rm -rf $(BUILD)

-include $(DEPS)

.PHONY: all test clean
