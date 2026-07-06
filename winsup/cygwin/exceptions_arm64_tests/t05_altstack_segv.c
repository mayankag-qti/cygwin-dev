/* t05_altstack_segv.c
 *
 * Integration test for the alternate-stack inline asm path in
 * _cygtls::call_signal_handler (the asm block we patched).
 *
 * THEORY
 * ------
 *   When a thread overflows its stack, SIGSEGV is delivered.  If a
 *   handler was installed with SA_ONSTACK and an alternate signal
 *   stack was configured via sigaltstack(), the handler MUST run on
 *   the alternate stack.  The handler then siglongjmps back to a safe
 *   recovery point.
 *
 *   This is the canonical real-world test of the alt-stack asm path:
 *     - new_sp argument is non-NULL (alt stack is set)
 *     - all the str/ldr around new_sp on x9 must work
 *     - the call to altstack_wrapper must preserve everything
 *       claimed in the clobber list (and the C around it must
 *       not have caller-saved values lost).
 *
 * WHAT THIS TEST DOES
 * -------------------
 *   1. Install SIGSEGV handler with SA_ONSTACK + SA_SIGINFO.
 *   2. Configure an alt-stack via sigaltstack().
 *   3. Call a deeply recursive function until the stack overflows.
 *   4. The handler siglongjmps back to main, which prints PASS.
 *
 *   Before the fix: may crash because clobber list is too small AND/OR
 *                   the handler's FP state corrupts caller code.
 *   After  the fix: clean PASS.
 *
 *   Note: success is binary -- either we get back from the handler or
 *   we don't.  The interesting failure modes are crash, infinite loop,
 *   or bogus signal info.
 */

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ALTSTACK_SIZE (SIGSTKSZ * 8)

static sigjmp_buf recover;
static volatile sig_atomic_t got_segv = 0;
static volatile sig_atomic_t got_signo = 0;

static void
segv_handler (int sig, siginfo_t *si, void *uctx)
{
  (void) si; (void) uctx;
  got_segv = 1;
  got_signo = sig;
  /* siglongjmp out of the alt stack back to main's setjmp. */
  siglongjmp (recover, 1);
}

/* Recursion designed to defeat -O optimization. */
static int
recurse (int depth, volatile char *prev)
{
  volatile char buf[1024];
  buf[0] = depth & 0xff;
  buf[sizeof buf - 1] = (depth >> 8) & 0xff;
  if (prev)
    buf[1] = prev[0];
  /* Use buf so the compiler can't elide the frame. */
  return recurse (depth + 1, buf) + buf[0];
}

int
main (void)
{
  stack_t ss;
  struct sigaction sa;
  void *altstack;

  altstack = malloc (ALTSTACK_SIZE);
  if (!altstack)
    {
      perror ("malloc altstack");
      return 1;
    }
  memset (altstack, 0xa5, ALTSTACK_SIZE);

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
  sa.sa_sigaction = segv_handler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
  sigemptyset (&sa.sa_mask);
  if (sigaction (SIGSEGV, &sa, NULL) != 0)
    {
      perror ("sigaction SIGSEGV");
      return 1;
    }
  /* Some systems route stack overflow as SIGBUS; cover that too. */
  if (sigaction (SIGBUS, &sa, NULL) != 0)
    {
      perror ("sigaction SIGBUS");
      return 1;
    }

  if (sigsetjmp (recover, 1) == 0)
    {
      (void) recurse (0, NULL);
      /* Should not reach here. */
      puts ("FAIL: recursion did not overflow");
      return 1;
    }

  printf ("recovered from signal %d on alt stack\n", (int) got_signo);
  if (got_segv && (got_signo == SIGSEGV || got_signo == SIGBUS))
    {
      puts ("PASS");
      return 0;
    }
  puts ("FAIL");
  return 1;
}
