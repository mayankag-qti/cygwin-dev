/* t03_null_uclink.c
 *
 * Targets BUG 3: NULL uc_link branch in __cont_link_context calls
 * cygwin_exit(0) instead of cygwin_exit(0xff).
 *
 * THEORY OF THE BUG
 * -----------------
 *   Original buggy trampoline:
 *
 *       cbz   x0, 1f         # if uc_link == 0 jump to 1:
 *       bl    setcontext
 *       mov   w0, #0xff      # exit code -- supposed to be used in NULL case
 *     1:
 *       bl    cygwin_exit    # ends up with w0 = 0 in NULL case!
 *
 *   When uc_link == NULL the cbz branch fires, JUMPS OVER `mov w0, #0xff`,
 *   and reaches `bl cygwin_exit` with w0 still equal to the lower 32 bits
 *   of x0 (which cbz proved to be 0).  Result: cygwin_exit(0) instead of
 *   cygwin_exit(0xff).
 *
 *   The fix restructures the flow with cbnz so the NULL branch executes
 *   `mov w0, #0xff` BEFORE tail-calling cygwin_exit.
 *
 * WHAT THIS TEST DOES
 * -------------------
 *   This program is a *child* program meant to be launched by the driver
 *   script t03_run.sh.  It:
 *     1. Builds a context with uc_link = NULL.
 *     2. swapcontext into it.
 *     3. The child function runs and falls off its end.
 *     4. __cont_link_context detects NULL uc_link and calls
 *        cygwin_exit(<code>).
 *
 *   The driver inspects the child's exit code:
 *     Before fix: exit code 0  (BUG)
 *     After  fix: exit code 255 (0xff)  (CORRECT)
 *
 *   The C program itself just exercises the path; it does not classify
 *   PASS / FAIL.  That is the driver's job.
 *
 * IMPORTANT
 * ---------
 *   The function MUST NOT call exit() or printf-flush on its way out --
 *   we need the exit to go through __cont_link_context.  We also avoid
 *   stdio after swapcontext for the same reason.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

static ucontext_t child_ctx;
static char child_stack[256 * 1024];

static void
child_fn (void)
{
  /* Just return.  The trampoline must take over. */
}

int
main (void)
{
  if (getcontext (&child_ctx) != 0)
    {
      fprintf (stderr, "getcontext failed\n");
      return 2; /* distinguished from the trampoline's exit codes */
    }
  child_ctx.uc_stack.ss_sp = child_stack;
  child_ctx.uc_stack.ss_size = sizeof child_stack;
  child_ctx.uc_link = NULL;	/* <-- key */

  makecontext (&child_ctx, child_fn, 0);

  /* Make sure we have written everything before transferring control. */
  fflush (stdout);
  fflush (stderr);

  /* setcontext never returns; control will reach __cont_link_context
     via child_fn returning. */
  setcontext (&child_ctx);

  /* If we reach here, the trampoline did not take over.  Distinguish
     this from the bug case by returning 3. */
  fprintf (stderr, "setcontext returned -- trampoline did not run\n");
  return 3;
}
