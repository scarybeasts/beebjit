#ifndef BEEBJIT_EXPRESSION_H
#define BEEBJIT_EXPRESSION_H

#include <stdint.h>

struct expression_struct;

struct expression_struct* expression_create(int64_t (*p_variable_read_callback)
                                                (void* p,
                                                 const char* p_name,
                                                 uint32_t index),
                                            void (*p_variable_write_callback)
                                                (void* p,
                                                 const char* p_name,
                                                 uint32_t index,
                                                 int64_t value),
                                            void* p_variable_object);
void expression_destroy(struct expression_struct* p_expression);

int64_t expression_parse(struct expression_struct* p_expression,
                         const char* p_expr_str);

const char* expression_get_original_string(
    struct expression_struct* p_expression);
uint32_t expression_get_tree_size(struct expression_struct* p_expression);
int64_t expression_execute(struct expression_struct* p_expression);

#endif /* BEEBJIT_EXPRESSION_H */
