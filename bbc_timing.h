#ifndef BEEBJIT_BBC_TIMING_H
#define BEEBJIT_BBC_TIMING_H

struct bbc_timing {
  void* p_callback_obj;
  void (*sync_tick_callback)(void* p);
};

#endif /* BEEBJIT_BBC_TIMING_H */
