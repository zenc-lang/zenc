# OS detection and portable commands
ifeq ($(OS),Windows_NT)
    EXE = .exe
    RM = rm -rf
    MKDIR = mkdir -p
    CP = cp -af
    LN = ln -sf
    INSTALL = install
else
    EXE =
    RM = rm -rf
    MKDIR = mkdir -p
    CP = cp -af
    LN = ln -sf
    INSTALL = install
endif

# Compiler configuration
# Default: gcc
# To build with clang: make CC=clang
# To build with zig:   make CC="zig cc"
# Version synchronization
GIT_VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "0.1.0")
CFLAGS = -Wall -Wextra -Wshadow -g -I./src -I./src/ast -I./src/parser -I./src/codegen -I./plugins -I./src/zen -I./src/utils -I./src/lexer -I./src/analysis -I./src/lsp -I./src/diagnostics -I./std/third-party/tre/include -DZEN_VERSION=\"$(GIT_VERSION)\" -DZEN_SHARE_DIR=\"$(SHAREDIR)\"
TARGET = zc$(EXE)
ifeq ($(OS),Windows_NT)
    LIBS = -lws2_32
else
    LIBS = -lm -lpthread -ldl
endif

SRCS = src/main.c \
       src/parser/parser_core.c \
       src/parser/parser_expr.c \
       src/parser/parser_stmt.c \
       src/parser/parser_type.c \
       src/parser/parser_utils.c \
       src/parser/parser_decl.c \
       src/parser/parser_struct.c \
       src/ast/ast.c \
       src/ast/primitives.c \
       src/ast/symbols.c \
       src/codegen/codegen.c \
       src/codegen/codegen_stmt.c \
       src/codegen/codegen_decl.c \
       src/codegen/codegen_main.c \
       src/codegen/codegen_utils.c \
       src/utils/utils.c \
       src/utils/colors.c \
       src/utils/cmd.c \
       src/platform/os.c \
       src/platform/console.c \
       src/platform/dylib.c \
       src/utils/config.c \
       src/diagnostics/diagnostics.c \
       src/lexer/token.c \
       src/analysis/typecheck.c \
       src/analysis/move_check.c \
       src/analysis/const_fold.c \
       src/lsp/json_rpc.c \
       src/lsp/lsp_main.c \
       src/lsp/lsp_analysis.c \
       src/lsp/lsp_semantic.c \
       src/lsp/lsp_index.c \
       src/lsp/lsp_formatter.c \
       src/lsp/lsp_project.c \
       src/lsp/cJSON.c \
       src/zen/zen_facts.c \
       src/repl/repl.c \
       src/plugins/plugin_manager.c \
       std/third-party/tre/lib/regcomp.c \
       std/third-party/tre/lib/regerror.c \
       std/third-party/tre/lib/regexec.c \
       std/third-party/tre/lib/tre-ast.c \
       std/third-party/tre/lib/tre-compile.c \
       std/third-party/tre/lib/tre-filter.c \
       std/third-party/tre/lib/tre-match-approx.c \
       std/third-party/tre/lib/tre-match-backtrack.c \
       std/third-party/tre/lib/tre-match-parallel.c \
       std/third-party/tre/lib/tre-mem.c \
       std/third-party/tre/lib/tre-parse.c \
       std/third-party/tre/lib/tre-stack.c \
       std/third-party/tre/lib/xmalloc.c

OBJ_DIR = obj
OBJS = $(patsubst %.c, $(OBJ_DIR)/%.o, $(SRCS))

# Installation paths
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man
SHAREDIR = $(PREFIX)/share/zenc
INCLUDEDIR = $(PREFIX)/include/zenc

PLUGINS = plugins/befunge.so plugins/brainfuck.so plugins/forth.so plugins/lisp.so plugins/sql.so

# APE (Actually Portable Executable) configuration
COSMOCC = cosmocc
OUT_STAGE = out/stage
OUT_BIN = out/bin
ZC_ENTRY_O = $(OUT_STAGE)/zc_entry.o
ZC_COM_BIN = $(OUT_STAGE)/zc.com
ZC_COM = $(OUT_BIN)/zc.com
ZC_BOOT_SRC = ape/boot/boot.zc
ZC_BOOT_COM_BIN = $(OUT_STAGE)/zc-boot.com
ZC_BOOT_COM = $(OUT_BIN)/zc-boot.com

# Default target
all: $(TARGET) $(PLUGINS)

# APE target
ape: $(ZC_COM) $(ZC_BOOT_COM)

# Build plugins

plugins/%.so: plugins/%.zc $(TARGET)
	./zc build $< -shared -o $@

# Link
$(TARGET): $(OBJS)
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "=> Build complete: $(TARGET)"

# Compile
$(OBJ_DIR)/%.o: %.c
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# APE targets
$(ZC_ENTRY_O): ape/zc_entry.c
	@$(MKDIR) $(@D)
	$(COSMOCC) -c $< -o $@

$(ZC_COM_BIN): $(ZC_ENTRY_O) $(SRCS)
	@$(MKDIR) $(@D)
	$(MAKE) \
		PLUGINS= \
		CC=$(COSMOCC) \
		OBJ_DIR=obj-ape \
		ZEN_VERSION="$(GIT_VERSION)" \
		LIBS="$(abspath $(ZC_ENTRY_O)) -Wl,--wrap=main" \
		TARGET="$(abspath $@)";

$(ZC_COM): $(ZC_COM_BIN)
	@$(MKDIR) $(@D)
	@$(CP) $(ZC_COM_BIN) $(wildcard $(ZC_COM_BIN).*) "$(@D)"; \
	zip -r "$(abspath $@)" std.zc std LICENSE;

$(ZC_BOOT_COM_BIN): $(ZC_BOOT_SRC) $(ZC_COM)
	@$(MKDIR) $(@D)
	./$(ZC_COM) build --cc $(COSMOCC) -o $@ $<

$(ZC_BOOT_COM): $(ZC_BOOT_COM_BIN) ape/boot/.args
	@$(MKDIR) $(@D)
	@$(CP) $(ZC_BOOT_COM_BIN) $(wildcard $(ZC_BOOT_COM_BIN).*) "$(@D)"; \
	(cd ape/boot && zip "$(abspath $@)" .args hello.zc instructions.txt Makefile); \
	zip "$(abspath $@)" LICENSE;

# Install
install: $(TARGET)
	$(INSTALL) -d $(BINDIR)
	$(INSTALL) -m 755 $(TARGET) $(BINDIR)/$(TARGET)
	
	# Install man pages
	$(INSTALL) -d $(MANDIR)/man1 $(MANDIR)/man5 $(MANDIR)/man7
	test -f man/zc.1 && $(INSTALL) -m 644 man/zc.1 $(MANDIR)/man1/zc.1 || true
	test -f man/zc.5 && $(INSTALL) -m 644 man/zc.5 $(MANDIR)/man5/zc.5 || true
	test -f man/zc.7 && $(INSTALL) -m 644 man/zc.7 $(MANDIR)/man7/zc.7 || true
	test -f man/zc-stdlib.7 && $(INSTALL) -m 644 man/zc-stdlib.7 $(MANDIR)/man7/zc-stdlib.7 || true
	
	# Install standard library
	$(INSTALL) -d $(SHAREDIR)
	$(CP) std $(SHAREDIR)/
	
	# Install facts
	$(INSTALL) -m 644 src/zen/facts.json $(SHAREDIR)/facts.json
	$(INSTALL) -m 644 src/repl/docs.json $(SHAREDIR)/docs.json
	$(INSTALL) -m 644 src/misc/zenc.json $(SHAREDIR)/zenc.json
	
	# Install plugin headers
	$(INSTALL) -d $(INCLUDEDIR)
	$(INSTALL) -m 644 plugins/zprep_plugin.h $(INCLUDEDIR)/zprep_plugin.h
	
	# Install compiled plugins
	$(INSTALL) -d $(SHAREDIR)/plugins
	$(CP) plugins/*.so $(SHAREDIR)/plugins/
	@echo "=> Installed to $(BINDIR)/$(TARGET)"
	@echo "=> Man pages installed to $(MANDIR)"
	@echo "=> Standard library installed to $(SHAREDIR)/std"

# Uninstall
uninstall:
	$(RM) $(BINDIR)/$(TARGET)
	$(RM) $(MANDIR)/man1/zc.1
	$(RM) $(MANDIR)/man5/zc.5
	$(RM) $(MANDIR)/man7/zc.7
	$(RM) $(MANDIR)/man7/zc-stdlib.7
	$(RM) $(SHAREDIR)
	@echo "=> Uninstalled from $(BINDIR)/$(TARGET)"
	@echo "=> Removed man pages from $(MANDIR)"
	@echo "=> Removed $(SHAREDIR)"

# Install APE
install-ape: ape
	$(INSTALL) -d $(BINDIR)
	$(INSTALL) -m 755 $(ZC_COM) $(BINDIR)/zc.com
	$(INSTALL) -m 755 $(ZC_BOOT_COM) $(BINDIR)/zc-boot.com
	$(LN) $(BINDIR)/zc.com $(BINDIR)/zc
	
	# Install standard library (shared)
	$(INSTALL) -d $(SHAREDIR)
	$(CP) std $(SHAREDIR)/
	@echo "=> Installed APE binaries to $(BINDIR)"
	@echo "=> Alias 'zc' points to zc.com"
	@echo "=> Standard library installed to $(SHAREDIR)/std"

# Uninstall APE
uninstall-ape:
	$(RM) $(BINDIR)/zc
	$(RM) $(BINDIR)/zc.com
	$(RM) $(BINDIR)/zc-boot.com
	$(RM) $(SHAREDIR)
	@echo "=> Uninstalled APE binaries from $(BINDIR)"
	@echo "=> Removed $(SHAREDIR)"

# Clean
clean:
	$(RM) $(OBJ_DIR) obj-ape $(TARGET) out.c plugins/*.so a.out* out
	@echo "=> Clean complete!"

# Test
# Supports running specific tests:
#	make test only="tests/std/test_hash.zc examples/arena_test.zc"
test: $(TARGET) $(PLUGINS)
	./tests/scripts/run_tests.sh -- $(filter %.zc,$(only))
	./tests/scripts/run_codegen_tests.sh $(filter %.zc,$(only))
	./tests/scripts/run_example_transpile.sh $(filter %.zc,$(only))

test-tcc: $(TARGET) $(PLUGINS)
	./tests/scripts/run_tests.sh --cc tcc

test-lsp: $(TARGET) $(PLUGINS)
	@echo "=> Building LSP Test Runner"
	$(CC) $(CFLAGS) tests/compiler/lsp/lsp_test_runner.c src/lsp/cJSON.c -o tests/compiler/lsp/test_runner
	@echo "=> Running LSP Tests"
	./tests/compiler/lsp/test_runner

# Build with alternative compilers
zig:
	$(MAKE) CC="zig cc"

clang:
	$(MAKE) CC=clang

windows:
	$(MAKE) CC="x86_64-w64-mingw32-gcc" TARGET="zc.exe" UI_OS="Windows" LIBS="-static -lm -lpthread"

asan: CFLAGS += -fsanitize=address,undefined -O1 -fno-omit-frame-pointer
asan: LIBS += -fsanitize=address,undefined
asan: $(TARGET) $(PLUGINS)

test-asan: clean asan
	ASAN_OPTIONS=detect_leaks=0 ./tests/scripts/run_tests.sh
	ASAN_OPTIONS=detect_leaks=0 ./tests/scripts/run_codegen_tests.sh
	ASAN_OPTIONS=detect_leaks=0 ./tests/scripts/run_example_transpile.sh

test-plugins: $(TARGET) $(PLUGINS)
	./zc run tests/plugins_suite.zc

.PHONY: all clean install uninstall install-ape uninstall-ape test zig clang ape windows asan test-asan test-plugins