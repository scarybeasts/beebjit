/* Appends at the end of expression.c. */

#include "test.h"

static void
expression_test_basic(void) {
  struct expression_struct* p_expression = expression_create();

  expression_parse(p_expression, "");
  test_expect_u32(0, expression_get_tree_size(p_expression));
  expression_parse(p_expression, " ");
  test_expect_u32(0, expression_get_tree_size(p_expression));

  expression_parse(p_expression, "7");
  test_expect_u32(1, expression_get_tree_size(p_expression));
  test_expect_u32(7, expression_execute(p_expression));

  expression_parse(p_expression, "1 + 2");
  test_expect_u32(3, expression_execute(p_expression));
  expression_parse(p_expression, "1+2");
  test_expect_u32(3, expression_execute(p_expression));

  expression_destroy(p_expression);
}

static void
expression_test_left_to_right(void) {
  struct expression_struct* p_expression = expression_create();

  expression_parse(p_expression, "1 + 2 + 3");
  test_expect_u32(6, expression_execute(p_expression));

  expression_parse(p_expression, "10 - 2 + 3");
  test_expect_u32(11, expression_execute(p_expression));

  expression_destroy(p_expression);
}

static void
expression_test_precedence(void) {
  struct expression_struct* p_expression = expression_create();

  expression_parse(p_expression, "1 * 2 + 3");
  test_expect_u32(5, expression_execute(p_expression));

  expression_parse(p_expression, "1 + 2 * 3");
  test_expect_u32(7, expression_execute(p_expression));

  expression_destroy(p_expression);
}

void
expression_test() {
  expression_test_basic();
  expression_test_left_to_right();
  expression_test_precedence();
}
