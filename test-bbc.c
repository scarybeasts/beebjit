/* Appends at the end of bbc.c. */

#include "test.h"

#include "state_6502.h"

static void
bbc_test_power_on_reset(struct bbc_struct* p_bbc) {
  uint8_t val;
  struct state_6502* p_state_6502 = bbc_get_6502(p_bbc);
  struct via_struct* p_system_via = bbc_get_sysvia(p_bbc);
  struct mc6850_struct* p_serial = bbc_get_serial(p_bbc);

  bbc_power_on_reset(p_bbc);

  /* 1) A test for 6502 reset. */
  test_expect_u32(0, state_6502_has_irq_high(p_state_6502));
  test_expect_u32(0, state_6502_has_nmi_high(p_state_6502));
  /* Enable CA1 in IER. */
  via_write_raw(p_system_via, 0xE, 0x82);
  via_set_CA1(p_system_via, 1);
  test_expect_u32(0, state_6502_has_irq_high(p_state_6502));
  via_set_CA1(p_system_via, 0);
  test_expect_u32(1, state_6502_has_irq_high(p_state_6502));

  /* Reset wasn't resetting IRQ lines! */
  bbc_power_on_reset(p_bbc);
  test_expect_u32(0, state_6502_has_irq_high(p_state_6502));

  /* 2) A test for MC6850 reset. */
  val = mc6850_read(p_serial, 0);
  test_expect_u32(0x02, val);
  mc6850_set_DCD(p_serial, 1);
  mc6850_set_CTS(p_serial, 1);
  val = mc6850_read(p_serial, 0);
  test_expect_u32(0x0C, val);

  bbc_power_on_reset(p_bbc);
  val = mc6850_read(p_serial, 0);
  test_expect_u32(0x02, val);
}

void
bbc_test(struct bbc_struct* p_bbc) {
  bbc_test_power_on_reset(p_bbc);
}
