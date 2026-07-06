# ARM64 `exceptions.cc` Standalone Test Suite

This suite verifies the four ARM64-specific fixes applied to
`winsup/cygwin/exceptions.cc`. It is designed for the cross-build flow
documented in `build-cygwin.sh`:

```
toolchain/aarch64-pc-cygwin/         <- prebuilt toolchain (its OWN cygwin1.dll)
build/aarch64-pc-cygwin/winsup/cygwin/
    new-cygwin1.dll                  <- your fresh DLL (under test)
    libcygwin.a                      <- your fresh import library
install/                             <- optional `make install` tree
```

## The "two cygwin1.dll" problem and how this suite handles it

You correctly observed that test executables CANNOT be expected to run
against a different cygwin1.dll than the one they were linked against.
A Cygwin program is bound by symbol+ordinal to a specific DLL's export
table, and the `_cygtls`, signal layout, etc. must match the DLL it
loads at runtime. So:

- You **cannot** compile a test against the toolchain's `libcygwin.a`
  and run it against your fresh DLL.
- You **cannot** compile a test against your fresh `libcygwin.a` and
  run it against the toolchain's DLL.

The fix mirrors what Cygwin's own `winsup/testsuite/` does, but without
requiring a MinGW cross-compiler:

1. **Build the test EXEs against your fresh `libcygwin.a`.** That binds
   them to your fresh DLL's exports. The toolchain's GCC binary, while
   compiling, runs as its own process under its own cygwin1.dll -- that
   coexistence is fine because GCC and the test EXE are separate
   processes.

2. **Launch the test EXEs via `cmd.exe`,** the native Windows command
   interpreter that ships with every Windows installation. `cmd.exe`
   has no `cygwin1.dll` dependency at all, so its process tree does
   not pre-load the toolchain's DLL. We invoke it as
   `cmd.exe /d /c "set PATH=...& test.exe"`. cmd processes the `set
   PATH` itself, then spawns `test.exe` as a fresh Windows process
   that loads whichever cygwin1.dll appears first on PATH.

3. **Set the child's PATH to a sandbox containing only your fresh
   cygwin1.dll** plus the Windows system directory. The toolchain's
   bin directory and `/usr/bin` are deliberately excluded so they
   cannot shadow your DLL.

The bash shell that runs `run_all.sh` continues to use whatever Cygwin
DLL your shell was started with -- but it never loads any test EXE,
so there is no conflict.

Why not just run `./test.exe` directly from bash? Because Cygwin's
`fork`/`exec` machinery hands off TLS and signal state from a parent
Cygwin process to a child Cygwin process, and that handoff requires
a compatible `cygwin1.dll` layout on both sides. Going through
`cmd.exe` (a non-Cygwin process) breaks that chain: the test starts
as a vanilla Windows process and loads cygwin1.dll fresh from PATH.

## Quick start

From this directory:

```bash
# 1. Build cygrun.exe + all tests, populate sandbox, run everything.
make check
```

If `make` cannot find your fresh DLL or import library, override the
paths:

```bash
make check \
    TOOLCHAIN_DIR=/c/work/cygwin-dev/toolchain/aarch64-pc-cygwin \
    CYGWIN_BUILD=/c/work/cygwin-dev/build/aarch64-pc-cygwin/winsup/cygwin
```

## Useful sub-targets

| Target | Action |
|---|---|
| `make show-config` | Print discovered paths and verify the fresh DLL exists. |
| `make all` | Build all `tNN_*.exe`. |
| `make sandbox` | Copy the fresh `cygwin1.dll` into `./sandbox/`. |
| `make check` | All of the above plus run `run_all.sh`. |
| `make clean` | Remove built EXEs and logs (keep sandbox). |
| `make distclean` | Remove sandbox too. |

## Per-test descriptions

| File | Bug | What it does |
|---|---|---|
| `t00_smoke_makecontext.c` | (baseline) | 8-arg `makecontext` round-trip. |
| `t01_makecontext_uclink_alignment.c` | **Bug 1** | argc 9..13 to expose ODD-parity `uc_link` slot misalignment. |
| `t02_makecontext_unwind.c` | **Bug 2** | Self-`raise(SIGUSR1)` from inside makecontext'd function while combining bug 1 stack layout — exercises SEH unwinder over `__cont_link_context`. |
| `t03_null_uclink.c` | **Bug 3** | Builds context with `uc_link=NULL`; runner checks exit code 0 (bug) vs 255 (fixed). |
| `t04_altstack_clobbers.c` | **Bug 4** | Probabilistic FP-divergence test under SIGUSR1 storm. |
| `t04b_clobber_register_witness.c` | **Bug 4** | Deterministic register-witness test (ARM64-only). |
| `t05_altstack_segv.c` | (integration) | Stack-overflow recovery via `SA_ONSTACK`. |
| `t06_swapcontext_loop.c` | (integration) | Combines all four bugs in one stress test. |

## Interpreting results on a buggy vs fixed DLL

Run `make check` once with your unfixed DLL, save `logs/summary.txt`,
then re-run with the fixed DLL.

| Test | Buggy DLL | Fixed DLL |
|---|---|---|
| t00_smoke | PASS | PASS |
| t01_uclink_alignment | CRASH (argc=9,11,13) | PASS |
| t02_unwind | varies (CRASH/FAIL) | PASS |
| t03_null_uclink | FAIL (exit 0) | PASS (exit 255) |
| t04_altstack_clobbers | sometimes FAIL | PASS |
| t04b_register_witness | sometimes FAIL | PASS |
| t05_altstack_segv | varies | PASS |
| t06_swapcontext_loop | varies (CRASH common) | PASS |

The two **most reliable** diagnostic tests are **t01** and **t03**:

- **t01** crashes deterministically on a buggy DLL because the SP
  alignment fault is hardware-enforced; it cannot be missed.
- **t03** has a binary, deterministic outcome — the exit code is 0 on
  the buggy DLL and 255 on the fixed DLL.

If both t01 and t03 pass, you have very strong evidence that bugs 1
and 3 are fixed. t02, t04, t04b, t05, t06 add coverage for the more
timing-dependent bugs 2 and 4.

## Troubleshooting

### "ERROR: fresh cygwin1.dll not found"

The Makefile looks in two locations:
1. `$(CYGWIN_BUILD)/new-cygwin1.dll`
2. `$(CYGWIN_INSTALL)/bin/cygwin1.dll`

If neither exists, override on the command line:

```bash
make CYGWIN_BUILD=/path/to/your/build/.../winsup/cygwin
```

Confirm with `make show-config`.

### Test EXE fails with "The procedure entry point ... could not be located"

This means the test was linked against the wrong libcygwin.a (probably
the toolchain's, not your fresh one). Check `make show-config` and
ensure `Fresh libA:` points into your build tree.

### Test EXE runs but loads the WRONG cygwin1.dll

Likely PATH is leaking. Verify the `Child PATH` line printed by
`run_all.sh` does not contain `/usr/bin` or the toolchain's `bin/`
directory. You can confirm which cygwin1.dll is loaded by running
from a vanilla CMD prompt:

```
C:\> set PATH=C:\path\to\sandbox;C:\Windows\System32;C:\Windows
C:\> tasklist /m cygwin1.dll
```

or by using Sysinternals Process Explorer to inspect the test
process while it is running.

### "rc=255" reported for tests OTHER than t03_null_uclink

That's an unexpected exit and counts as CRASH. 255 is special-cased
only for `t03_null_uclink` because that test deliberately invokes
`cygwin_exit(0xff)` via the trampoline.
