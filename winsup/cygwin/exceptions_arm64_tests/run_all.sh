#!/usr/bin/env bash
# run_all.sh -- standalone runner for the ARM64 exceptions.cc test suite.
#
# This script reuses the EXACT mechanism Cygwin's own winsup/testsuite uses
# to launch its tests, namely:
#
#     cygdrop $mingwtestdir/cygrun.exe <test.exe>
#
# - cygrun.exe is a small MinGW (NOT Cygwin) x86_64 launcher built by
#   the testsuite. Because it has no cygwin1.dll dependency, it launches
#   the test EXE as a fresh Windows process that loads cygwin1.dll fresh
#   from PATH.
#
# - cygdrop ("Cygwin drop") is a Cygwin utility that strips Cygwin-specific
#   parent/child handoff state before running its argument. This is what
#   makes it safe to launch a Cygwin EXE built against a DIFFERENT
#   cygwin1.dll than the parent shell uses, without TLS/signal-state
#   handoff corruption.
#
# Both tools are already present on your system and are used by your
# passing `make check` of the main Cygwin testsuite, so we know this
# launching pipeline works.
#
# We just point them at our own test EXEs (built against your fresh
# libcygwin.a) and our own sandbox (containing your fresh cygwin1.dll).

set -u

cd "$(dirname "$0")"

: "${SANDBOX:=$(pwd)/sandbox}"
: "${CYGRUN:=}"
: "${CYGDROP:=$(command -v cygdrop || echo /usr/bin/cygdrop)}"

# Auto-discover cygrun.exe from the Cygwin testsuite build tree if not
# overridden. We expect it next to the main testsuite Makefile.
if [ -z "$CYGRUN" ]; then
    # Default location matches build-cygwin.sh.
    DEFAULT_CYGRUN="$(realpath ../../../build/aarch64-pc-cygwin/winsup/testsuite/mingw/cygrun.exe 2>/dev/null || true)"
    if [ -x "$DEFAULT_CYGRUN" ]; then
        CYGRUN="$DEFAULT_CYGRUN"
    fi
fi

# Sanity checks.
if [ ! -f "$SANDBOX/cygwin1.dll" ]; then
    echo "ERROR: sandbox cygwin1.dll not found at $SANDBOX/cygwin1.dll"
    echo "       run 'make sandbox' first"
    exit 2
fi
if [ ! -x "$CYGDROP" ]; then
    echo "ERROR: cygdrop not found (expected at $CYGDROP)"
    echo "       install it or set CYGDROP=/full/path/to/cygdrop"
    exit 2
fi
if [ -z "$CYGRUN" ] || [ ! -x "$CYGRUN" ]; then
    echo "ERROR: cygrun.exe not found"
    echo "       Expected at:"
    echo "         build/aarch64-pc-cygwin/winsup/testsuite/mingw/cygrun.exe"
    echo "       Override with: CYGRUN=/path/to/cygrun.exe make check"
    exit 2
fi

# Build the PATH the test EXE will see. We want the sandbox first so
# the loader binds the test to YOUR DLL, not the toolchain's or the
# host system's. Then minimal Windows directories so kernel32 etc.
# are findable.
WINDIR_NATIVE=$(cygpath -w "${SYSTEMROOT:-C:/Windows}")
SANDBOX_WIN=$(cygpath -w "$SANDBOX")
CHILD_PATH_WIN="${SANDBOX_WIN};${WINDIR_NATIVE}\\System32;${WINDIR_NATIVE}"

# POSIX form for setting PATH from bash. cygdrop/cygrun handle the
# Cygwin->Windows PATH translation when they spawn the child.
SANDBOX_POSIX="$SANDBOX"
WIN_SYSTEM32_POSIX=$(cygpath -u "${SYSTEMROOT:-C:/Windows}/System32")
WIN_DIR_POSIX=$(cygpath -u "${SYSTEMROOT:-C:/Windows}")
CHILD_PATH_POSIX="${SANDBOX_POSIX}:${WIN_SYSTEM32_POSIX}:${WIN_DIR_POSIX}"

mkdir -p logs
> logs/summary.txt

pass=0
fail=0
crash=0

# ---------------------------------------------------------------------
# run_test <symbolic-name> <test-exe-path>
# ---------------------------------------------------------------------
run_test () {
    local name="$1"
    local exe="$2"
    local log="logs/${name}.log"
    local rc

    if [ ! -e "$exe" ]; then
        printf "[ SKIP ] %s (binary not built)\n" "$name"
        echo "SKIP $name" >> logs/summary.txt
        return
    fi

    printf "[ RUN  ] %s\n" "$name"

    # Launch the test the same way Cygwin's own testsuite does:
    #
    #     PATH=<sandbox>:... cygdrop <cygrun.exe> <test.exe>
    #
    # cygdrop forks/execs cygrun without parent-child Cygwin state
    # handoff, then cygrun (a MinGW program with no cygwin1.dll
    # dependency) calls CreateProcess on test.exe. test.exe starts as
    # a vanilla Windows process and loads cygwin1.dll fresh from the
    # PATH it inherits, which we control to point at the sandbox.
    #
    # Use /usr/bin/timeout (Cygwin coreutils) explicitly because the
    # subshell PATH includes Windows System32 which has its own
    # incompatible timeout.exe.
    GNU_TIMEOUT=/usr/bin/timeout

    if [ -x "$GNU_TIMEOUT" ]; then
        ( PATH="$CHILD_PATH_POSIX" \
          CYGWIN_TESTING=1 \
          "$GNU_TIMEOUT" 60 "$CYGDROP" "$CYGRUN" "$exe" \
        ) > "$log" 2>&1
        rc=$?
    else
        ( PATH="$CHILD_PATH_POSIX" \
          CYGWIN_TESTING=1 \
          "$CYGDROP" "$CYGRUN" "$exe" \
        ) > "$log" 2>&1
        rc=$?
    fi

    last=$(tail -n 1 "$log" 2>/dev/null)
    case "$rc" in
        0)
            if [ "$last" = "PASS" ]; then
                printf "[  OK  ] %s\n" "$name"
                pass=$((pass+1))
                printf "PASS %s\n" "$name" >> logs/summary.txt
            else
                # rc=0 but no PASS line: t03 special case (cygwin_exit(0)
                # would land here -- BUG 3 present on a buggy DLL).
                if [ "$name" = "t03_null_uclink" ]; then
                    printf "[ FAIL ] %s (exit 0 -- BUG 3 PRESENT)\n" "$name"
                    fail=$((fail+1))
                    printf "FAIL %s exit=0_BUG_3\n" "$name" >> logs/summary.txt
                else
                    printf "[ FAIL ] %s (rc=0, last='%s')\n" "$name" "$last"
                    fail=$((fail+1))
                    printf "FAIL %s rc=0 no_pass_line\n" "$name" >> logs/summary.txt
                fi
            fi
            ;;
        124)
            printf "[ HANG ] %s\n" "$name"
            crash=$((crash+1))
            printf "HANG %s\n" "$name" >> logs/summary.txt
            ;;
        255)
            # t03_null_uclink expects 0xff on a fixed DLL.
            if [ "$name" = "t03_null_uclink" ]; then
                printf "[  OK  ] %s (exit 0xff confirms fix)\n" "$name"
                pass=$((pass+1))
                printf "PASS %s exit=0xff\n" "$name" >> logs/summary.txt
            else
                printf "[CRASH ] %s (rc=%d)\n" "$name" "$rc"
                crash=$((crash+1))
                printf "CRASH %s rc=%d\n" "$name" "$rc" >> logs/summary.txt
            fi
            ;;
        139|134|136|11)
            printf "[CRASH ] %s (rc=%d)\n" "$name" "$rc"
            crash=$((crash+1))
            printf "CRASH %s rc=%d\n" "$name" "$rc" >> logs/summary.txt
            ;;
        127)
            printf "[ FAIL ] %s (rc=127 -- launcher could not run; check %s)\n" \
                   "$name" "$log"
            fail=$((fail+1))
            printf "FAIL %s rc=127\n" "$name" >> logs/summary.txt
            ;;
        *)
            printf "[ FAIL ] %s (rc=%d)\n" "$name" "$rc"
            fail=$((fail+1))
            printf "FAIL %s rc=%d\n" "$name" "$rc" >> logs/summary.txt
            ;;
    esac
}

echo ""
echo "Sandbox cygwin1.dll: $SANDBOX/cygwin1.dll"
echo "cygdrop            : $CYGDROP"
echo "cygrun.exe         : $CYGRUN"
echo "Child PATH (POSIX) : $CHILD_PATH_POSIX"
echo "Child PATH (Win)   : $CHILD_PATH_WIN"
echo ""
if [ -x /usr/bin/sha256sum ]; then
    echo "Sandbox DLL hash:"
    sha256sum "$SANDBOX/cygwin1.dll"
fi
echo ""

run_test t00_smoke                      "./t00_smoke_makecontext.exe"
run_test t01_uclink_alignment           "./t01_makecontext_uclink_alignment.exe"
run_test t02_unwind                     "./t02_makecontext_unwind.exe"
run_test t03_null_uclink                "./t03_null_uclink.exe"
run_test t04_altstack_clobbers          "./t04_altstack_clobbers.exe"
run_test t04b_register_witness          "./t04b_clobber_register_witness.exe"
run_test t05_altstack_segv              "./t05_altstack_segv.exe"
run_test t06_swapcontext_loop           "./t06_swapcontext_loop.exe"

echo ""
echo "==================================================="
echo "Summary: $pass passed, $fail failed, $crash crashed/hung"
echo "==================================================="
cat logs/summary.txt

if [ $fail -gt 0 ] || [ $crash -gt 0 ]; then
    exit 1
fi
exit 0
