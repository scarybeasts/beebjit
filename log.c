#include "log.h"

#include <assert.h>
#include <err.h>
#include <stdio.h>

static const char*
log_module_to_string(int module) {
  switch (module) {
  case k_log_video:
    return "video";
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
    return "UNUSUAL";
  case k_log_unimplemented:
    return "UNIMPLEMENTED";
  default:
    assert(0);
    return NULL;
  }
}

void
log_do_log_int1(int module, int severity, const char* p_msg, int ival1) {
  const char* p_module_str = log_module_to_string(module);
  const char* p_severity_str = log_severity_to_string(severity);
  int ret = fprintf(stdout, 
                    "%s:%s:%s %d\n",
                    p_severity_str,
                    p_module_str,
                    p_msg,
                    ival1);
  if (ret <= 0) {
    errx(1, "fprintf failed");
  }
}
