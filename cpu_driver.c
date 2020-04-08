#include "cpu_driver.h"

#include "interp.h"
#include "inturbo.h"
#include "jit.h"
#include "util.h"

#include <assert.h>
#include <stddef.h>

static void
cpu_driver_memory_range_invalidate_dummy(struct cpu_driver* p_cpu_driver,
                                         uint16_t addr,
                                         uint32_t len) {
  (void) p_cpu_driver;
  (void) addr;
  (void) len;
}

static char*
cpu_driver_get_address_info_dummy(struct cpu_driver* p_cpu_driver,
                                  uint16_t addr) {
  (void) p_cpu_driver;
  (void) addr;

  return "    ";
}

static void
cpu_driver_get_custom_counters_dummy(struct cpu_driver* p_cpu_driver,
                                     uint64_t* p_c1,
                                     uint64_t* p_c2) {
  (void) p_cpu_driver;

  *p_c1 = 0;
  *p_c2 = 0;
}

static void
cpu_driver_set_reset_callback_default(
    struct cpu_driver* p_cpu_driver,
    void (*do_reset_callback)(void* p, uint32_t flags),
    void* p_do_reset_callback_object) {
  p_cpu_driver->do_reset_callback = do_reset_callback;
  p_cpu_driver->p_do_reset_callback_object = p_do_reset_callback_object;
}

static void
cpu_driver_apply_flags_default(struct cpu_driver* p_cpu_driver,
                               uint32_t flags_set,
                               uint32_t flags_clear) {
  p_cpu_driver->flags |= flags_set;
  p_cpu_driver->flags &= ~flags_clear;
}

static uint32_t
cpu_driver_get_flags_default(struct cpu_driver* p_cpu_driver) {
  return p_cpu_driver->flags;
}

static uint32_t
cpu_driver_get_exit_value_default(struct cpu_driver* p_cpu_driver) {
  return p_cpu_driver->exit_value;
}

static void
cpu_driver_set_exit_value_default(struct cpu_driver* p_cpu_driver,
                                  uint32_t exit_value) {
  p_cpu_driver->exit_value = exit_value;
}

struct cpu_driver*
cpu_driver_alloc(int mode,
                 struct state_6502* p_state_6502,
                 struct memory_access* p_memory_access,
                 struct timing_struct* p_timing,
                 struct bbc_options* p_options) {
  struct cpu_driver* p_cpu_driver = NULL;
  struct cpu_driver_funcs* p_funcs =
      util_mallocz(sizeof(struct cpu_driver_funcs));

  switch (mode) {
  case k_cpu_mode_interp:
    p_cpu_driver = interp_create(p_funcs);
    if (p_cpu_driver == NULL) {
      util_bail("interp_create() failed");
    }
    break;
  case k_cpu_mode_inturbo:
    p_cpu_driver = inturbo_create(p_funcs);
    if (p_cpu_driver == NULL) {
      util_bail("inturbo_create() failed");
    }
    break;
  case k_cpu_mode_jit:
    p_cpu_driver = jit_create(p_funcs);
    if (p_cpu_driver == NULL) {
      util_bail("jit_create() failed");
    }
    break;
  default:
    assert(0);
    break;
  }

  asm_x64_abi_init(&p_cpu_driver->abi,
                   p_memory_access,
                   p_options,
                   p_state_6502);

  p_cpu_driver->p_memory_access = p_memory_access;
  p_cpu_driver->p_timing = p_timing;
  p_cpu_driver->p_options = p_options;
  p_cpu_driver->p_funcs = p_funcs;

  p_funcs->set_reset_callback = cpu_driver_set_reset_callback_default;
  p_funcs->apply_flags = cpu_driver_apply_flags_default;
  p_funcs->get_flags = cpu_driver_get_flags_default;
  p_funcs->get_exit_value = cpu_driver_get_exit_value_default;
  p_funcs->set_exit_value = cpu_driver_set_exit_value_default;
  p_funcs->memory_range_invalidate = cpu_driver_memory_range_invalidate_dummy;
  p_funcs->get_address_info = cpu_driver_get_address_info_dummy;
  p_funcs->get_custom_counters = cpu_driver_get_custom_counters_dummy;

  p_funcs->init(p_cpu_driver);

  return p_cpu_driver;
}
