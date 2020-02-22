#ifndef BEEBJIT_LOG_H
#define BEEBJIT_LOG_H

enum log_module {
  k_log_null_module = 0,
  k_log_perf = 1,
  k_log_video = 2,
  k_log_disc = 3,
  k_log_instruction = 4,
  k_log_serial = 5,
  k_log_jit = 6,
  k_log_keyboard = 7,
  k_log_misc = 8,
};

enum log_severity {
  k_log_null_severity = 0,
  k_log_info = 1,
  k_log_unusual = 2,
  k_log_unimplemented = 3,
  k_log_error = 4,
};

void log_do_log(int module, int severity, const char* p_msg, ...);

#endif /* BEEBJIT_LOG_H */
