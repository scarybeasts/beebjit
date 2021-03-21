#ifndef BEEBJIT_EXPRESSION_H
#define BEEBJIT_EXPRESSION_H

#include <stdint.h>

struct expression_struct;

struct expression_struct* expression_create(void);
void expression_destroy(struct expression_struct* p_expression);

int64_t expression_parse(struct expression_struct* p_expression,
                         const char* p_expr_str);

uint32_t expression_get_tree_size(struct expression_struct* p_expression);
int64_t expression_execute(struct expression_struct* p_expression);

#endif /* BEEBJIT_EXPRESSION_H */
