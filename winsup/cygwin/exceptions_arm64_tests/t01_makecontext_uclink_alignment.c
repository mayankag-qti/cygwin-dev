/* t01_makecontext_uclink_alignment.c
 *
 * Targets BUG 1: `mov sp, x19` makes SP transiently unaligned in
 * __cont_link_context.
 *
 * THEORY OF THE BUG
 * -----------------
 *   In makecontext (ARM64), the uc_link slot is placed at sp[stack_args],
 *   where sp itself is 16-byte aligned.  So the uc_link slot lives at
 *   offset 8*stack_args bytes from a 16-aligned base.  That offset is
 *   16-byte aligned only when stack_args is EVEN.
 *
 *   When stack_args is ODD, _MC_uclinkReg = sp + stack_args ends in ...8,
 *   i.e. only 8-byte aligned.  The buggy trampoline does
 *
 *       mov sp, x19
 *       ldr x0, [sp]
 *
 *   which on Windows ARM64 raises an SP-alignment fault when x19 is
 *   unaligned, or crashes inside `bl setcontext` due to bad LR/SP state.
 *
 *   stack_args = max(0, argc - 8).
 *   stack_args ODD  => argc in {9, 11, 13, ...}
 *   stack_args EVEN => argc in {8, 10, 12, ...}
 *
 * WHAT THIS TEST DOES
 * -------------------
 *   Builds a context for each value of argc in {9, 10, 11, 12, 13}, runs
 *   it, verifies that arguments arrive correctly AND that uc_link is
 *   honored (i.e. control returns to main).
 *
 *   Before the fix: ODD-parity cases (argc=9, 11, 13) crash on ARM64.
 *   After  the fix: all five sub-cases PASS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

static ucontext_t main_ctx, child_ctx;
static char child_stack[512 * 1024];
static volatile int args_ok;

#define CHECK(idx, val) do { if ((val) != (idx)) args_ok = 0; } while (0)

static void
f9 (int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9)
{
  args_ok = 1;
  CHECK (1, a1); CHECK (2, a2); CHECK (3, a3); CHECK (4, a4);
  CHECK (5, a5); CHECK (6, a6); CHECK (7, a7); CHECK (8, a8);
  CHECK (9, a9);
}

static void
f10 (int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
     int a9, int a10)
{
  args_ok = 1;
  CHECK (1, a1); CHECK (2, a2); CHECK (3, a3); CHECK (4, a4);
  CHECK (5, a5); CHECK (6, a6); CHECK (7, a7); CHECK (8, a8);
  CHECK (9, a9); CHECK (10, a10);
}

static void
f11 (int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
     int a9, int a10, int a11)
{
  args_ok = 1;
  CHECK (1, a1); CHECK (2, a2); CHECK (3, a3); CHECK (4, a4);
  CHECK (5, a5); CHECK (6, a6); CHECK (7, a7); CHECK (8, a8);
  CHECK (9, a9); CHECK (10, a10); CHECK (11, a11);
}

static void
f12 (int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
     int a9, int a10, int a11, int a12)
{
  args_ok = 1;
  CHECK (1, a1); CHECK (2, a2); CHECK (3, a3); CHECK (4, a4);
  CHECK (5, a5); CHECK (6, a6); CHECK (7, a7); CHECK (8, a8);
  CHECK (9, a9); CHECK (10, a10); CHECK (11, a11); CHECK (12, a12);
}

static void
f13 (int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
     int a9, int a10, int a11, int a12, int a13)
{
  args_ok = 1;
  CHECK (1, a1); CHECK (2, a2); CHECK (3, a3); CHECK (4, a4);
  CHECK (5, a5); CHECK (6, a6); CHECK (7, a7); CHECK (8, a8);
  CHECK (9, a9); CHECK (10, a10); CHECK (11, a11); CHECK (12, a12);
  CHECK (13, a13);
}

static int
run_case (int argc_v)
{
  args_ok = 0;

  if (getcontext (&child_ctx) != 0)
    return -1;
  child_ctx.uc_stack.ss_sp = child_stack;
  child_ctx.uc_stack.ss_size = sizeof child_stack;
  child_ctx.uc_link = &main_ctx;

  switch (argc_v)
    {
    case 9:
      makecontext (&child_ctx, (void (*) (void)) f9, 9,
		   1, 2, 3, 4, 5, 6, 7, 8, 9);
      break;
    case 10:
      makecontext (&child_ctx, (void (*) (void)) f10, 10,
		   1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
      break;
    case 11:
      makecontext (&child_ctx, (void (*) (void)) f11, 11,
		   1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
      break;
    case 12:
      makecontext (&child_ctx, (void (*) (void)) f12, 12,
		   1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
      break;
    case 13:
      makecontext (&child_ctx, (void (*) (void)) f13, 13,
		   1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
      break;
    default:
      return -1;
    }

  /* If the trampoline crashes, we never come back from this call. */
  if (swapcontext (&main_ctx, &child_ctx) != 0)
    return -1;

  return args_ok ? 0 : -1;
}

int
main (void)
{
  static const int cases[] = { 9, 10, 11, 12, 13 };
  int failures = 0;
  size_t i;

  for (i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
      int argc_v = cases[i];
      int stack_args = argc_v - 8;
      int parity = stack_args & 1;
      int rc;

      fflush (stdout);
      rc = run_case (argc_v);
      printf ("argc=%2d stack_args=%d parity=%-4s : %s\n",
	      argc_v, stack_args, parity ? "ODD" : "EVEN",
	      rc == 0 ? "ok" : "FAIL");
      if (rc != 0)
	failures++;
    }

  if (failures == 0)
    {
      puts ("PASS");
      return 0;
    }
  printf ("FAIL: %d sub-cases failed\n", failures);
  return 1;
}
