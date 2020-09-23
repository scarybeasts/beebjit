#ifndef BEEBJIT_LOG_H
#define BEEBJIT_LOG_H

#include <stdint.h>

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
  k_log_audio = 9,
  k_log_tape = 10,
  k_log_cmos = 11,
};

enum log_severity {
  k_log_null_severity = 0,
  k_log_info = 1,
  k_log_unusual = 2,
  k_log_unimplemented = 3,
  k_log_error = 4,
  k_log_warning = 5,
};

void log_set_log_filename(const char* p_filename);
void log_set_do_log_to_stdout(int do_log_to_stdout);

void log_do_log(int module, int severity, const char* p_msg, ...)
    __attribute__((format(printf, 3, 4)));
void log_do_log_max_count(uint32_t* p_counter,
                          int module,
                          int severity,
                          const char* p_msg,
                          ...)
    __attribute__((format(printf, 4, 5)));

#endif /* BEEBJIT_LOG_H */
