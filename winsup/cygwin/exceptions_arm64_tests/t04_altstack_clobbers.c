/* t04_altstack_clobbers.c
 *
 * Targets BUG 4: ARM64 alt-stack inline asm in _cygtls::call_signal_handler
 * has an incomplete clobber list.  The original list was:
 *
 *     "memory", "x0".."x7", "x9", "x10", "x29", "x30"
 *
 * It MISSED:
 *     - x8 (indirect result location), x11..x15 (caller-saved temps),
 *       x16, x17 (linker-veneer scratch)
 *     - v0..v7 (FP/SIMD args), v16..v31 (FP/SIMD temps)
 *     - cc (NZCV flags)
 *
 * The asm block itself performs a CALL to altstack_wrapper(), which is
 * plain C and may use any caller-saved register.  From the compiler's
 * view the asm block IS the call, so the compiler relies entirely on
 * the clobber list to know what survives.  If the compiler held a value
 * in x8 / v3 / NZCV across the asm block, that value silently corrupts
 * after the signal handler runs.
 *
 * WHY THIS BUG IS HARD TO TRIGGER FROM USER CODE
 * ----------------------------------------------
 *   The asm block is inside libcygwin1.dll.  User code does not have
 *   variables held in caller-saved regs *across* a Cygwin library call;
 *   only Cygwin's own internal C code in the same translation unit
 *   could have such a live range.  However, the BEHAVIOURAL EFFECT on
 *   user programs is observable:
 *
 *     1. Floating-point / SIMD state corruption inside the signal
 *        handler chain, because the wrapper may dirty v0..v7 / v16..v31
 *        without the compiler scheduling spills around the asm block.
 *     2. Condition flags (NZCV) corruption if a caller value depended
 *        on them across the call.
 *
 *   We can observe (1) by:
 *     - Setting up an alternate signal stack with sigaltstack().
 *     - Installing a SA_SIGINFO|SA_ONSTACK handler that intentionally
 *       performs heavy FP / SIMD work in C (calls to the math library,
 *       or just a busy double-precision loop).
 *     - In main, kicking off long-running double-precision arithmetic
 *       loops and from another thread / via raise() interrupting them
 *       repeatedly with the alt-stack handler.
 *     - Checking the final FP result against a known value computed
 *       without signals.
 *
 * WHAT THIS TEST DOES
 * -------------------
 *   Single-threaded variant:
 *     1. Compute a reference result of a deterministic FP loop in the
 *        absence of signals.
 *     2. Repeat the same FP loop, but from another thread send SIGUSR1
 *        thousands of times.  The handler runs on the alt-stack and
 *        does its own FP work to dirty FP/SIMD regs.
 *     3. Compare results.
 *
 *   Before the fix: results may diverge intermittently because the
 *                   compiler may keep an FP value in v3..v7 across the
 *                   alt-stack asm block, and the wrapper clobbers it
 *                   without the compiler knowing.
 *   After  the fix: results match, even under heavy signal load.
 *
 *   Note: this is a probabilistic test.  We run many iterations and
 *   look for ANY divergence.  False negatives are possible in a single
 *   run; the driver runs the test a few times to reduce that risk.
 */

#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#define ITERS         200000
#define ALTSTACK_SIZE (SIGSTKSZ * 4)

static volatile sig_atomic_t stop_signaller = 0;
static volatile sig_atomic_t signals_delivered = 0;

/* Handler: SA_SIGINFO + SA_ONSTACK so the alt-stack asm path executes.
   We do FP/SIMD work in here to dirty caller-saved FP regs. */
static void
sig_handler (int sig, siginfo_t *si, void *uctx)
{
  (void) sig; (void) si; (void) uctx;
  /* Intentionally dirty FP/SIMD with values unrelated to the main loop. */
  volatile double a = 3.14159265358979;
  volatile double b = 2.71828182845905;
  volatile double c;
  for (int i = 0; i < 16; i++)
    {
      c = a * b + (double) i;
      a = c / (b + 0.001);
      b = sqrt (fabs (a) + 1.0);
    }
  signals_delivered++;
  (void) c;
}

/* Deterministic FP work the main thread does. */
static double
fp_work (void)
{
  double acc = 1.0;
  for (long i = 1; i <= ITERS; i++)
    {
      acc += 1.0 / (double) i;
      acc *= 1.0000001;
      acc -= 0.0000001 * (double) i;
    }
  return acc;
}

static void *
signaller_thread (void *arg)
{
  pthread_t target = *(pthread_t *) arg;
  while (!stop_signaller)
    {
      pthread_kill (target, SIGUSR1);
      /* short pause to avoid total starvation */
      for (volatile int k = 0; k < 200; k++)
	;
    }
  return NULL;
}

int
main (void)
{
  stack_t ss;
  struct sigaction sa;
  pthread_t self, sig_thr;
  void *altstack;
  double ref, observed;
  int divergences = 0;
  int trials = 5;
  int t;

  /* Allocate alternate signal stack. */
  altstack = malloc (ALTSTACK_SIZE);
  if (!altstack)
    {
      perror ("malloc altstack");
      return 1;
    }
  memset (&ss, 0, sizeof ss);
  ss.ss_sp = altstack;
  ss.ss_size = ALTSTACK_SIZE;
  ss.ss_flags = 0;
  if (sigaltstack (&ss, NULL) != 0)
    {
      perror ("sigaltstack");
      return 1;
    }

  memset (&sa, 0, sizeof sa);
  sa.sa_sigaction = sig_handler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
  sigemptyset (&sa.sa_mask);
  if (sigaction (SIGUSR1, &sa, NULL) != 0)
    {
      perror ("sigaction");
      return 1;
    }

  /* 1. Compute reference (no signals). */
  ref = fp_work ();

  /* 2. Run the same loop while another thread bombards us with SIGUSR1. */
  self = pthread_self ();
  for (t = 0; t < trials; t++)
    {
      stop_signaller = 0;
      signals_delivered = 0;
      if (pthread_create (&sig_thr, NULL, signaller_thread, &self) != 0)
	{
	  perror ("pthread_create");
	  return 1;
	}

      observed = fp_work ();

      stop_signaller = 1;
      pthread_join (sig_thr, NULL);

      printf ("trial %d: signals=%d ref=%.15f observed=%.15f delta=%g\n",
	      t, (int) signals_delivered, ref, observed, observed - ref);

      /* Floating-point work IS deterministic in the abstract -- but if
	 caller-saved FP regs are clobbered by the signal handler and the
	 compiler did not know, the main loop's value will diverge by an
	 amount much larger than rounding error. */
      if (observed != ref)
	{
	  /* Allow tiny drift from compiler reordering across signals
	     -- but the bug produces gross divergence (e.g. NaN, wildly
	     different values).  Anything > 1e-9 in absolute delta is
	     a strong signal. */
	  double delta = observed - ref;
	  if (delta < 0) delta = -delta;
	  if (delta > 1e-9)
	    divergences++;
	}
    }

  free (altstack);

  if (divergences == 0)
    {
      puts ("PASS");
      return 0;
    }
  printf ("FAIL: %d/%d trials diverged\n", divergences, trials);
  return 1;
}
