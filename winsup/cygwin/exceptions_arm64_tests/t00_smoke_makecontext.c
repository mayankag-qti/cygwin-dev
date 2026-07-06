/* t00_smoke_makecontext.c
 *
 * Smoke test: basic makecontext / swapcontext functionality on ARM64.
 * Verifies that:
 *   - A context can be built and switched into.
 *   - Arguments arrive correctly (8 register args, 0 stack args).
 *   - uc_link is honored: when the function returns, control transfers to
 *     the linked context (NOT exit).
 *
 * This is the "does the basic flow even work" test. If this fails after
 * the fix, every other test will fail too.
 *
 * Expected result on a correct cygwin1.dll: prints PASS, exits 0.
 *
 * Before/after analysis:
 *   - On a *broken* DLL with bug 1 (unaligned SP), this MAY crash because
 *     stack_args == 0 puts uc_link at sp+0 which is 16-aligned, so it
 *     usually escapes. This test is a baseline.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

static ucontext_t main_ctx, child_ctx;
static char child_stack[256 * 1024];
static volatile int saw_args_ok = 0;
static volatile int saw_returned = 0;

static void
child_func (int a, int b, int c, int d, int e, int f, int g, int h)
{
  if (a == 1 && b == 2 && c == 3 && d == 4
      && e == 5 && f == 6 && g == 7 && h == 8)
    saw_args_ok = 1;
  else
    fprintf (stderr, "ARG MISMATCH: %d %d %d %d %d %d %d %d\n",
	     a, b, c, d, e, f, g, h);
  /* Falling off the end must transfer to uc_link. */
}

int
main (void)
{
  if (getcontext (&child_ctx) != 0)
    {
      perror ("getcontext");
      return 1;
    }
  child_ctx.uc_stack.ss_sp = child_stack;
  child_ctx.uc_stack.ss_size = sizeof child_stack;
  child_ctx.uc_link = &main_ctx;

  makecontext (&child_ctx, (void (*) (void)) child_func, 8,
	       1, 2, 3, 4, 5, 6, 7, 8);

  if (swapcontext (&main_ctx, &child_ctx) != 0)
    {
      perror ("swapcontext");
      return 1;
    }
  saw_returned = 1;

  if (saw_args_ok && saw_returned)
    {
      puts ("PASS");
      return 0;
    }
  printf ("FAIL: args_ok=%d returned=%d\n", saw_args_ok, saw_returned);
  return 1;
}
