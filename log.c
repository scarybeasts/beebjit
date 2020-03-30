#include "log.h"

#include "util.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

static const char*
log_module_to_string(int module) {
  switch (module) {
  case k_log_perf:
    return "perf";
  case k_log_video:
    return "video";
  case k_log_disc:
    return "disc";
  case k_log_instruction:
    return "instruction";
  case k_log_serial:
    return "serial";
  case k_log_jit:
    return "jit";
  case k_log_keyboard:
    return "keyboard";
  case k_log_misc:
    return "misc";
  case k_log_audio:
    return "audio";
  default:
    assert(0);
    return NULL;
  }
}

static const char*
log_severity_to_string(int severity) {
  switch (severity) {
  case k_log_info:
    return "info";
  case k_log_unusual:
    return "unusual";
  case k_log_unimplemented:
    return "unimplemented";
  case k_log_error:
    return "ERROR";
  case k_log_warning:
    return "WARNING";
  default:
    assert(0);
    return NULL;
  }
}

void
log_do_log(int module, int severity, const char* p_msg, ...) {
  va_list args;
  char msg[256];
  int ret;

  const char* p_module_str = log_module_to_string(module);
  const char* p_severity_str = log_severity_to_string(severity);

  va_start(args, p_msg);
  ret = vsnprintf(msg, sizeof(msg), p_msg, args);
  va_end(args);
  if (ret <= 0) {
    util_bail("vsnprintf failed");
  }

  ret = fprintf(stdout, "%s:%s:%s\n", p_severity_str, p_module_str, msg);
  if (ret <= 0) {
    util_bail("fprintf failed");
  }
  ret = fflush(stdout);
  if (ret != 0) {
    util_bail("fflush failed");
  }
}
