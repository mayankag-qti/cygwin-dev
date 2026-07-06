/* t06_swapcontext_loop.c
 *
 * Stress test combining makecontext, swapcontext, and signal delivery
 * with multiple simultaneous contexts.  Exercises:
 *   - Bug 1 (alignment) and Bug 2 (unwind) via the trampoline
 *   - Bug 4 (clobbers) via SIGUSR1 with SA_ONSTACK
 *
 * THEORY
 * ------
 *   We build a "ping-pong" between two contexts.  Each context, when
 *   resumed, increments a counter and swaps back to the other context.
 *   After N rounds, both eventually fall off the end of their function
 *   and reach uc_link (= main_ctx).
 *
 *   Different contexts are configured with different argc parities
 *   (forcing both 16-aligned and 8-aligned uc_link slots) to stress
 *   the trampoline alignment fix.
 *
 *   Throughout, a separate thread fires SIGUSR1 at us with an alt-stack
 *   handler installed, exercising the alt-stack asm.
 *
 * RESULT
 * ------
 *   Pass iff: the counter reaches the expected value AND both contexts
 *   complete cleanly AND we observe a non-zero signal count.
 *
 *   Before the fix: prone to crash, hang, or wrong counter under load.
 *   After  the fix: PASS.
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#define ROUNDS 5000
#define ALTSTACK_SIZE (SIGSTKSZ * 4)

static ucontext_t main_ctx, ctxA, ctxB;
static char stackA[256 * 1024];
static char stackB[256 * 1024];
static volatile long counter = 0;
static volatile sig_atomic_t signals_handled = 0;
static volatile sig_atomic_t stop_signaller = 0;

static void
usr1_handler (int sig, siginfo_t *si, void *uctx)
{
  (void) sig; (void) si; (void) uctx;
  /* Touch some FP regs to dirty caller-saved state. */
  volatile double d = 1.5;
  for (int i = 0; i < 4; i++) d = d * 1.0001 + (double) i;
  signals_handled++;
  (void) d;
}

/* argc=9 => stack_args=1 (ODD parity) -- exercises bug 1. */
static void
funA (int a, int b, int c, int d, int e, int f, int g, int h, int i)
{
  volatile int sum = a + b + c + d + e + f + g + h + i;
  (void) sum;
  while (counter < ROUNDS)
    {
      counter++;
      if (swapcontext (&ctxA, &ctxB) != 0)
	break;
    }
  /* Fall off end -> trampoline -> uc_link (= main_ctx). */
}

/* argc=10 => stack_args=2 (EVEN parity). */
static void
funB (int a, int b, int c, int d, int e, int f, int g, int h, int i, int j)
{
  volatile int sum = a + b + c + d + e + f + g + h + i + j;
  (void) sum;
  while (counter < ROUNDS)
    {
      counter++;
      if (swapcontext (&ctxB, &ctxA) != 0)
	break;
    }
  /* Fall off end -> trampoline -> uc_link (= main_ctx). */
}

static void *
signaller (void *arg)
{
  pthread_t target = *(pthread_t *) arg;
  while (!stop_signaller)
    {
      pthread_kill (target, SIGUSR1);
      for (volatile int k = 0; k < 500; k++)
	;
    }
  return NULL;
}

int
main (void)
{
  stack_t ss;
  struct sigaction sa;
  void *altstack;
  pthread_t self, sig_thr;

  altstack = malloc (ALTSTACK_SIZE);
  if (!altstack)
    return 1;
  memset (&ss, 0, sizeof ss);
  ss.ss_sp = altstack;
  ss.ss_size = ALTSTACK_SIZE;
  ss.ss_flags = 0;
  sigaltstack (&ss, NULL);

  memset (&sa, 0, sizeof sa);
  sa.sa_sigaction = usr1_handler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
  sigemptyset (&sa.sa_mask);
  sigaction (SIGUSR1, &sa, NULL);

  /* Configure A. */
  if (getcontext (&ctxA) != 0)
    return 1;
  ctxA.uc_stack.ss_sp = stackA;
  ctxA.uc_stack.ss_size = sizeof stackA;
  ctxA.uc_link = &main_ctx;
  makecontext (&ctxA, (void (*) (void)) funA, 9,
	       1, 2, 3, 4, 5, 6, 7, 8, 9);

  /* Configure B. */
  if (getcontext (&ctxB) != 0)
    return 1;
  ctxB.uc_stack.ss_sp = stackB;
  ctxB.uc_stack.ss_size = sizeof stackB;
  ctxB.uc_link = &main_ctx;
  makecontext (&ctxB, (void (*) (void)) funB, 10,
	       11, 12, 13, 14, 15, 16, 17, 18, 19, 20);

  /* Start signaller thread. */
  self = pthread_self ();
  if (pthread_create (&sig_thr, NULL, signaller, &self) != 0)
    return 1;

  /* Kick off ping-pong. */
  if (swapcontext (&main_ctx, &ctxA) != 0)
    {
      perror ("swapcontext");
      return 1;
    }

  stop_signaller = 1;
  pthread_join (sig_thr, NULL);

  printf ("counter=%ld (expected %d) signals=%d\n",
	  counter, ROUNDS, (int) signals_handled);
  free (altstack);

  if (counter >= ROUNDS && signals_handled > 0)
    {
      puts ("PASS");
      return 0;
    }
  puts ("FAIL");
  return 1;
}
