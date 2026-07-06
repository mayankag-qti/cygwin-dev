# Makefile for ARM64 exceptions.cc regression tests
#
# DESIGN
# ------
# Two key principles dictate this Makefile:
#
# 1. The test EXEs MUST be linked against YOUR FRESHLY BUILT libcygwin.a
#    (not the toolchain's), so they bind to the cygwin1.dll containing
#    your fix.
#
#    We use the same trick winsup/testsuite/Makefile.am uses:
#        -nodefaultlibs <test.c> $(FRESH_LIBA) -lgcc -lkernel32 -luser32
#    -nodefaultlibs prevents GCC from auto-adding -lcygwin (which
#    would resolve to the toolchain's libcygwin.a and bind us to the
#    wrong DLL).
#
# 2. The test EXEs MUST be launched via the same NON-CYGWIN launcher
#    that Cygwin's own testsuite uses, so that:
#       (a) the launcher itself does not pre-load any cygwin1.dll;
#       (b) the parent->child Cygwin state handoff does not happen
#           (it would corrupt the test process because parent and
#           child have different cygwin1.dll layouts).
#
#    Cygwin's testsuite uses:
#        cygdrop $(builddir)/mingw/cygrun.exe <test.exe>
#    where cygrun.exe is a small MinGW launcher built from
#    winsup/testsuite/cygrun.c. We reuse that exact binary -- it is
#    built during your normal `make check` of the testsuite (see
#    build-cygwin.sh).
#
# WHAT YOU PROVIDE
# ----------------
#   TOOLCHAIN_DIR  Top of your prebuilt toolchain (must contain
#                  bin/aarch64-pc-cygwin-gcc.exe and the matching
#                  cygwin1.dll/headers). Default mirrors build-cygwin.sh:
#                    $(PWD)/../../../toolchain/aarch64-pc-cygwin
#
#   CYGWIN_BUILD   Top of your Cygwin BUILD directory (the one that
#                  contains build/aarch64-pc-cygwin/winsup/cygwin/...).
#                  Default mirrors build-cygwin.sh:
#                    $(PWD)/../../../build/aarch64-pc-cygwin/winsup/cygwin
#
#   CYGWIN_INSTALL Top of your Cygwin INSTALL tree (the one populated
#                  by `make install DESTDIR=...` in build-cygwin.sh).
#                  Default:
#                    $(PWD)/../../../install
#
# TARGETS
# -------
#   make all       Build all tNN_*.exe.
#   make check     Build everything, populate sandbox, run all tests.
#   make sandbox   Just create the sandbox directory with cygwin1.dll.
#   make clean     Remove built artifacts (keeps sandbox).
#   make distclean Remove everything including sandbox.
#
# Override on command line, e.g.:
#   make CYGWIN_BUILD=/c/work/build/aarch64-pc-cygwin/winsup/cygwin

# ---------------------------------------------------------------------------
# Paths (relative defaults that match build-cygwin.sh)
# ---------------------------------------------------------------------------
TOOLCHAIN_DIR  ?= $(abspath ../../../toolchain/aarch64-pc-cygwin)
CYGWIN_BUILD   ?= $(abspath ../../../build/aarch64-pc-cygwin/winsup/cygwin)
CYGWIN_INSTALL ?= $(abspath ../../../install)

# Sandbox is where we copy the freshly-built cygwin1.dll for runtime.
# Tests run with PATH=$(SANDBOX) prepended.
SANDBOX        ?= $(abspath ./sandbox)

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
TARGET_TRIPLE = aarch64-pc-cygwin
ARM64_GCC    ?= $(TOOLCHAIN_DIR)/bin/$(TARGET_TRIPLE)-gcc.exe
ARM64_AR     ?= $(TOOLCHAIN_DIR)/bin/$(TARGET_TRIPLE)-ar.exe

# ---------------------------------------------------------------------------
# Locate the freshly-built artifacts
# ---------------------------------------------------------------------------
# Prefer the build tree (no install step needed). Fall back to install tree.
FRESH_DLL_BUILD     := $(CYGWIN_BUILD)/new-cygwin1.dll
FRESH_DLL_INSTALLED := $(CYGWIN_INSTALL)/bin/cygwin1.dll
FRESH_LIBA_BUILD    := $(CYGWIN_BUILD)/libcygwin.a
FRESH_LIBA_INSTALLED:= $(CYGWIN_INSTALL)/lib/libcygwin.a

FRESH_DLL  := $(if $(wildcard $(FRESH_DLL_BUILD)),$(FRESH_DLL_BUILD),$(FRESH_DLL_INSTALLED))
FRESH_LIBA := $(if $(wildcard $(FRESH_LIBA_BUILD)),$(FRESH_LIBA_BUILD),$(FRESH_LIBA_INSTALLED))

# ---------------------------------------------------------------------------
# Compile/link flags
# ---------------------------------------------------------------------------
# Tests: link against the FRESH libcygwin.a (your build's import
# library). We use multiple tactics to ensure the linker picks our
# fresh libcygwin.a, not the toolchain's:
#
#   1. -L<dir-of-fresh-libcygwin>  (so -lcygwin finds ours first)
#   2. Explicit absolute path to our libcygwin.a, listed BEFORE any
#      flag that might pull in the toolchain's libcygwin.
#   3. -Wl,--disable-high-entropy-va (Cygwin testsuite flag, makes
#      the load address predictable for fork emulation).
#
# We do NOT use -nodefaultlibs here. The Cygwin testsuite uses it
# along with `binmode.o' from the build tree, but for our standalone
# tests it's simpler to let GCC's default startup chain do its job
# and just override the cygwin import library lookup via -L ordering.
COMMON_CFLAGS = -O0 -g -Wall -Wextra -Wno-unused-parameter \
                -fno-omit-frame-pointer

TEST_CFLAGS  = $(COMMON_CFLAGS)
# -L points at the dir containing the fresh libcygwin.a so the linker
# resolves -lcygwin (added implicitly by GCC's spec file) to OUR copy
# before consulting the toolchain's default library path.
TEST_LDFLAGS = -L$(dir $(FRESH_LIBA)) -Wl,--disable-high-entropy-va
TEST_LIBS    =

# Native Windows command interpreter (kept for reference; not used at
# runtime any more -- we now use cygdrop + cygrun.exe like the main
# Cygwin testsuite does).
CMD_EXE := $(shell cygpath -w "$${SYSTEMROOT:-C:/Windows}/System32/cmd.exe")

# Reuse the cygrun.exe built by the main Cygwin testsuite.
# build/aarch64-pc-cygwin/winsup/testsuite/mingw/cygrun.exe is built
# during your normal Cygwin build (see build-cygwin.sh: it runs
# `make check' for the testsuite, which builds cygrun first). It is a
# MinGW (non-Cygwin) x86_64 launcher used by the testsuite itself.
CYGRUN_DEFAULT := $(abspath ../../../build/aarch64-pc-cygwin/winsup/testsuite/mingw/cygrun.exe)
CYGRUN ?= $(CYGRUN_DEFAULT)

CYGDROP ?= $(shell command -v cygdrop 2>/dev/null || echo /usr/bin/cygdrop)

# ---------------------------------------------------------------------------
# File lists
# ---------------------------------------------------------------------------
TEST_SRCS = \
    t00_smoke_makecontext.c \
    t01_makecontext_uclink_alignment.c \
    t02_makecontext_unwind.c \
    t03_null_uclink.c \
    t04_altstack_clobbers.c \
    t04b_clobber_register_witness.c \
    t05_altstack_segv.c \
    t06_swapcontext_loop.c

TEST_EXES = $(TEST_SRCS:.c=.exe)

# ---------------------------------------------------------------------------
# Top-level rules
# ---------------------------------------------------------------------------
.PHONY: all check sandbox clean distclean show-config

all: show-config $(TEST_EXES)

show-config:
	@echo "Toolchain:   $(ARM64_GCC)"
	@echo "Fresh DLL:   $(FRESH_DLL)"
	@echo "Fresh libA:  $(FRESH_LIBA)"
	@echo "Sandbox:     $(SANDBOX)"
	@echo "cygdrop:     $(CYGDROP)"
	@echo "cygrun.exe:  $(CYGRUN)"
	@if [ ! -f "$(FRESH_DLL)" ]; then \
	    echo "ERROR: fresh cygwin1.dll not found at $(FRESH_DLL)"; \
	    echo "       set CYGWIN_BUILD=... or CYGWIN_INSTALL=... on the make line"; \
	    exit 1; \
	fi
	@if [ ! -f "$(FRESH_LIBA)" ]; then \
	    echo "ERROR: fresh libcygwin.a not found at $(FRESH_LIBA)"; \
	    exit 1; \
	fi
	@if [ ! -x "$(CYGRUN)" ]; then \
	    echo "ERROR: cygrun.exe not found at $(CYGRUN)"; \
	    echo "       this is built by 'make check' in winsup/testsuite"; \
	    echo "       (see build-cygwin.sh). Override with: CYGRUN=/path make check"; \
	    exit 1; \
	fi
	@if [ ! -x "$(CYGDROP)" ]; then \
	    echo "ERROR: cygdrop not found at $(CYGDROP)"; \
	    echo "       install Cygwin's cygdrop utility, or override with CYGDROP=..."; \
	    exit 1; \
	fi

# ---------------------------------------------------------------------------
# Test binaries -- bound to the FRESH cygwin1.dll
# ---------------------------------------------------------------------------
%.exe: %.c
	$(ARM64_GCC) $(TEST_CFLAGS) $(TEST_LDFLAGS) -o $@ $< $(TEST_LIBS)

# ---------------------------------------------------------------------------
# Sandbox: a directory containing ONLY the freshly built cygwin1.dll.
# Tests are run with PATH=$(SANDBOX) prepended, so the loader binds them
# to YOUR DLL and not the toolchain's.
# ---------------------------------------------------------------------------
sandbox: $(SANDBOX)/cygwin1.dll

$(SANDBOX)/cygwin1.dll: $(FRESH_DLL)
	@mkdir -p $(SANDBOX)
	cp -f $< $@

# ---------------------------------------------------------------------------
# Run the suite
# ---------------------------------------------------------------------------
check: all sandbox
	@CYGRUN='$(CYGRUN)' \
	 CYGDROP='$(CYGDROP)' \
	 SANDBOX='$(SANDBOX)' \
	 ./run_all.sh

# ---------------------------------------------------------------------------
# Cleanup
# ---------------------------------------------------------------------------
clean:
	rm -f *.exe *.o *.stackdump core.* logs/*.log logs/summary.txt

distclean: clean
	rm -rf $(SANDBOX) logs
