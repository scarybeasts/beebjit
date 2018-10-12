#ifndef BEEBJIT_BBC_OPTIONS_H
#define BEEBJIT_BBC_OPTIONS_H

struct bbc_options {
  /* External options. */
  int debug;

  /* Internal options, callback, etc. */
  void* (*debug_callback)(void* p);
  void* p_debug_callback_object;
};

#endif /* BEEBJIT_BBC_OPTIONS_H */
