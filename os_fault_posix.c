#include "os_fault.h"

#include "os_fault_platform.h"
#include "util.h"

#include <signal.h>
#include <string.h>
#include <unistd.h>

static void (*s_p_fault_callback)(uintptr_t*, uintptr_t, int, int, uintptr_t);

static void
posix_fault_handler(int signum, siginfo_t* p_siginfo, void* p_void) {
  uintptr_t host_fault_addr;
  uintptr_t host_pc;
  uintptr_t host_context;
  int is_exec_fault;
  int is_write_fault;

  /* Crash unless it's fault type we expected. */
  if ((signum == SIGSEGV) && (p_siginfo->si_code == SEGV_ACCERR)) {
    /* OK. */
  } else if ((signum == SIGBUS) && (p_siginfo->si_code == BUS_ADRALN)) {
    /* OK; hits on macOS for writing to the read-only JIT mapping.
     * Note that it's definitely not a bad alignment; that's just the code that
     * comes through as the others would likely make even less sense.
     */
  } else {
    os_fault_bail();
  }

  host_fault_addr = (uintptr_t) p_siginfo->si_addr;
  host_pc = os_fault_get_pc(p_void);
  host_context = os_fault_get_jit_context(p_void);
  is_exec_fault = os_fault_is_exec_fault(p_void);
  is_write_fault = os_fault_is_write_fault(p_void);

  s_p_fault_callback(&host_pc,
                     host_fault_addr,
                     is_exec_fault,
                     is_write_fault,
                     host_context);

  os_fault_set_pc(p_void, host_pc);
}

static void
install_handler(int signal) {
  struct sigaction sa;
  struct sigaction sa_prev;
  int ret;

  (void) memset(&sa, '\0', sizeof(sa));
  (void) memset(&sa_prev, '\0', sizeof(sa_prev));

  sa.sa_sigaction = posix_fault_handler;
  sa.sa_flags = (SA_SIGINFO | SA_NODEFER);
  ret = sigaction(signal, &sa, &sa_prev);
  if (ret != 0) {
    util_bail("sigaction failed");
  }
  if ((sa_prev.sa_sigaction != NULL) &&
      (sa_prev.sa_sigaction != posix_fault_handler)) {
    util_bail("conflicting fault handler");
  }
}

void
os_fault_register_handler(
    void (*p_fault_callback)(uintptr_t* p_host_rip,
                             uintptr_t host_fault_addr,
                             int is_exec,
                             int is_write,
                             uintptr_t host_rdi)) {
  s_p_fault_callback = p_fault_callback;

  install_handler(SIGSEGV);
  /* On macOS, a write fault to our JIT mapping comes in as SIGBUS. */
  install_handler(SIGBUS);
}

void
os_fault_bail(void) {
  struct sigaction sa;

  (void) memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = SIG_DFL;
  (void) sigaction(SIGSEGV, &sa, NULL);

  (void) raise(SIGSEGV);
  _exit(1);
}

void
os_debug_trap(void) {
  int ret = raise(SIGTRAP);
  if (ret != 0) {
    util_bail("raise failed");
  }
}
