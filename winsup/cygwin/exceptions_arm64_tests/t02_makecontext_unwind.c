/* t02_makecontext_unwind.c
 *
 * Targets BUG 2: `bl setcontext` / `bl cygwin_exit` in __cont_link_context
 * violate the ARM64 Windows SEH unwind contract.
 *
 * THEORY OF THE BUG
 * -----------------
 *   The buggy trampoline declares an empty prologue (.seh_endprologue
 *   right after the label) yet uses `bl` (Branch with Link) to call
 *   setcontext / cygwin_exit. `bl` clobbers x30 (LR), making the
 *   function technically non-leaf.  ARM64 Windows SEH requires non-leaf
 *   functions to declare an LR save in their unwind data.  When the
 *   unwinder later walks through this frame -- which it will if any
 *   exception is taken inside setcontext, or if a debugger / profiler
 *   captures a stack -- it can fault or produce a corrupted stack walk.
 *
 *   The fix uses `b` (plain branch / tail call) instead of `bl`, which
 *   does not clobber LR and keeps the function effectively leaf.
 *
 * WHAT THIS TEST DOES
 * -------------------
 *   1. Builds a context whose function will RAISE A SIGNAL (SIGUSR1)
 *      from inside the makecontext'd function.  When the signal arrives
 *      we set a flag in the handler and return.  The function then falls
 *      off the end -> hits __cont_link_context -> setcontext(uc_link)
 *      -> back to main.
 *
 *      Why does this exercise the unwinder?  Cygwin's signal delivery on
 *      ARM64 walks the stack to decide whether the thread is in a
 *      signal-safe state (see _cygtls::interrupt_now / inside_kernel
 *      in exceptions.cc).  Stack walking uses RtlVirtualUnwind, which
 *      consumes SEH unwind data for every frame on the stack -- INCLUDING
 *      __cont_link_context if it happens to be in the call chain.
 *
 *   2. Calls swapcontext-into and back-out 200 times, each time raising
 *      a SIGUSR1 from inside the child function.  If the unwinder ever
 *      faults inside __cont_link_context, the process aborts.
 *
 *   3. Combine with bug 1 by using argc=11 (odd stack_args) to put the
 *      uclink slot in an unaligned location.  This stresses both bugs
 *      simultaneously.
 *
 *   Before the fix: prone to crash or abort, especially under load.
 *   After  the fix: clean PASS.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static ucontext_t main_ctx, child_ctx;
static char child_stack[512 * 1024];
static volatile sig_atomic_t handler_hits = 0;

static void
sigusr1_handler (int sig)
{
  (void) sig;
  handler_hits++;
}

static void
child_fn (int a, int b, int c, int d, int e, int f, int g, int h,
	  int i, int j, int k)
{
  /* Use args so the compiler keeps them around, exercising stack-arg path. */
  volatile int sum = a + b + c + d + e + f + g + h + i + j + k;
  (void) sum;
  /* Self-signal.  This causes Cygwin to walk our stack from the signal
     thread, potentially through __cont_link_context. */
  raise (SIGUSR1);
  /* Falling off the end -> __cont_link_context -> setcontext(&main_ctx). */
}

int
main (void)
{
  struct sigaction sa;
  int rounds = 200;
  int i;

  memset (&sa, 0, sizeof sa);
  sa.sa_handler = sigusr1_handler;
  sa.sa_flags = 0;
  sigemptyset (&sa.sa_mask);
  if (sigaction (SIGUSR1, &sa, NULL) != 0)
    {
      perror ("sigaction");
      return 1;
    }

  for (i = 0; i < rounds; i++)
    {
      if (getcontext (&child_ctx) != 0)
	{
	  perror ("getcontext");
	  return 1;
	}
      child_ctx.uc_stack.ss_sp = child_stack;
      child_ctx.uc_stack.ss_size = sizeof child_stack;
      child_ctx.uc_link = &main_ctx;

      /* argc = 11 -> stack_args = 3 (ODD), exercises bug 1 too. */
      makecontext (&child_ctx, (void (*) (void)) child_fn, 11,
		   1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);

      if (swapcontext (&main_ctx, &child_ctx) != 0)
	{
	  perror ("swapcontext");
	  return 1;
	}
    }

  printf ("rounds=%d handler_hits=%d\n", rounds, (int) handler_hits);
  if (handler_hits == rounds)
    {
      puts ("PASS");
      return 0;
    }
  puts ("FAIL");
  return 1;
}
