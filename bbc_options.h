#ifndef BEEBJIT_BBC_OPTIONS_H
#define BEEBJIT_BBC_OPTIONS_H

struct bbc_options {
  /* External options. */
  int debug;
  const char* p_opt_flags;
  const char* p_log_flags;

  /* Internal options, callbacks, etc. */
  void* (*debug_callback)(void* p);
  void* p_debug_callback_object;
};

#endif /* BEEBJIT_BBC_OPTIONS_H */
