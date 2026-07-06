/* t04b_clobber_register_witness.c
 *
 * Deterministic complement to t04_altstack_clobbers.c.
 *
 * THEORY
 * ------
 *   Bug 4 manifests when the compiler holds a value in a caller-saved
 *   register that the inline-asm clobber list inside Cygwin fails to
 *   declare clobbered.  The asm block calls altstack_wrapper(), which
 *   may dirty any caller-saved register (x8, x11..x17, v0..v7, v16..v31,
 *   NZCV).
 *
 *   We cannot directly observe the clobber from user code because the
 *   buggy asm is INSIDE cygwin1.dll, not in our compilation unit.
 *   However, we CAN observe the same class of bug via this experiment:
 *
 *     - In our test, manually load a witness value into a caller-saved
 *       register (e.g. x12 or v3) using inline asm.
 *     - Trigger a signal that runs an SA_ONSTACK SA_SIGINFO handler.
 *     - The handler does FP / int work (which dirties caller-saved regs).
 *     - After the kernel-equivalent return path, read back the witness.
 *
 *   On a CORRECTLY behaving Cygwin signal-delivery path, the kernel's
 *   ucontext save/restore preserves user state across the handler -- so
 *   the witness should survive.  If Cygwin's alt-stack asm corrupts
 *   ucontext save/restore due to bug 4, the witness may be wrong on
 *   return.
 *
 *   Note: this still tests the system-level invariant rather than the
 *   exact asm clobber.  But it is FAR more sensitive than t04_altstack
 *   because we don't depend on the optimizer to keep a value in a
 *   specific register.
 *
 * WHAT THIS TEST DOES
 * -------------------
 *   In a tight loop:
 *     1. Set x12 = 0xDEAD0000 + i (or a chosen FP reg = known double).
 *     2. raise(SIGUSR1).
 *     3. Read back x12 (or the FP reg).
 *     4. Compare.
 *
 *   We do this for a few different witness registers (x12, x14, v3).
 *
 *   Before fix: any divergence = FAIL.
 *   After  fix: PASS.
 *
 *   This test is ARM64-only.
 */

#if !defined(__aarch64__)
#include <stdio.h>
int main (void) { puts ("SKIP: aarch64-only test"); return 0; }
#else

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ALTSTACK_SIZE (SIGSTKSZ * 4)
#define ITERS 5000

static void
sig_handler (int sig, siginfo_t *si, void *uctx)
{
  (void) sig; (void) si; (void) uctx;
  /* Aggressively dirty caller-saved regs that the buggy clobber list
     doesn't declare. */
  __asm__ __volatile__ (
    "mov x8,  #0xaaaa\n\t"
    "mov x11, #0xbbbb\n\t"
    "mov x12, #0xcccc\n\t"
    "mov x13, #0xdddd\n\t"
    "mov x14, #0xeeee\n\t"
    "mov x15, #0xffff\n\t"
    "fmov d0, #1.0\n\t"
    "fmov d1, #2.0\n\t"
    "fmov d2, #3.0\n\t"
    "fmov d3, #4.0\n\t"
    "fmov d4, #5.0\n\t"
    "fmov d5, #6.0\n\t"
    "fmov d6, #7.0\n\t"
    "fmov d7, #8.0\n\t"
    : : : "x8", "x11", "x12", "x13", "x14", "x15",
	  "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7");
}

static int
test_int_witness (void)
{
  int divergences = 0;

  for (int i = 0; i < ITERS; i++)
    {
      unsigned long want12 = 0xdead0000UL + (unsigned long) i;
      unsigned long want14 = 0xbeef0000UL + (unsigned long) i;
      unsigned long got12, got14;

      __asm__ __volatile__ (
	"mov x12, %[w12]\n\t"
	"mov x14, %[w14]\n\t"
	: : [w12] "r" (want12), [w14] "r" (want14)
	: "x12", "x14");

      raise (SIGUSR1);

      __asm__ __volatile__ (
	"mov %[g12], x12\n\t"
	"mov %[g14], x14\n\t"
	: [g12] "=r" (got12), [g14] "=r" (got14)
	: : );

      if (got12 != want12 || got14 != want14)
	{
	  if (divergences < 3)
	    fprintf (stderr,
		     "iter %d: x12 want=%lx got=%lx | x14 want=%lx got=%lx\n",
		     i, want12, got12, want14, got14);
	  divergences++;
	}
    }
  return divergences;
}

static int
test_fp_witness (void)
{
  int divergences = 0;

  for (int i = 0; i < ITERS; i++)
    {
      double want3 = 1234.5 + (double) i;
      double want6 = 9876.25 - (double) i;
      double got3, got6;

      /* Load doubles from memory directly into d3 and d6.
	 Using `ldr` with a memory operand avoids the constraint-name
	 mismatch that affects `fmov dN, %[w]` (where GCC emits a v-name
	 that fmov rejects). */
      __asm__ __volatile__ (
	"ldr d3, %[w3]\n\t"
	"ldr d6, %[w6]\n\t"
	: : [w3] "m" (want3), [w6] "m" (want6)
	: "d3", "d6");

      raise (SIGUSR1);

      __asm__ __volatile__ (
	"str d3, %[g3]\n\t"
	"str d6, %[g6]\n\t"
	: [g3] "=m" (got3), [g6] "=m" (got6)
	: : );

      if (got3 != want3 || got6 != want6)
	{
	  if (divergences < 3)
	    fprintf (stderr,
		     "iter %d: d3 want=%g got=%g | d6 want=%g got=%g\n",
		     i, want3, got3, want6, got6);
	  divergences++;
	}
    }
  return divergences;
}

int
main (void)
{
  stack_t ss;
  struct sigaction sa;
  void *altstack;
  int int_div, fp_div;

  altstack = malloc (ALTSTACK_SIZE);
  if (!altstack) return 1;
  memset (&ss, 0, sizeof ss);
  ss.ss_sp = altstack;
  ss.ss_size = ALTSTACK_SIZE;
  ss.ss_flags = 0;
  if (sigaltstack (&ss, NULL) != 0)
    { perror ("sigaltstack"); return 1; }

  memset (&sa, 0, sizeof sa);
  sa.sa_sigaction = sig_handler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
  sigemptyset (&sa.sa_mask);
  if (sigaction (SIGUSR1, &sa, NULL) != 0)
    { perror ("sigaction"); return 1; }

  int_div = test_int_witness ();
  fp_div  = test_fp_witness ();

  printf ("int divergences: %d / %d\n", int_div, ITERS);
  printf ("fp  divergences: %d / %d\n", fp_div,  ITERS);

  free (altstack);

  if (int_div == 0 && fp_div == 0)
    {
      puts ("PASS");
      return 0;
    }
  puts ("FAIL");
  return 1;
}

#endif /* __aarch64__ */
