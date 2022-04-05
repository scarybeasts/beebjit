#ifndef BEEBJIT_EXPRESSION_H
#define BEEBJIT_EXPRESSION_H

#include <stdint.h>

struct expression_struct;

typedef int64_t (*expression_var_read_func_t)(void* p, uint32_t index);
typedef void (*expression_var_write_func_t)(void* p,
                                            uint32_t index,
                                            int64_t value);
typedef expression_var_read_func_t (*expression_var_read_lookup_func_t)(
    void* p, const char* p_name);
typedef expression_var_write_func_t (*expression_var_write_lookup_func_t)(
    void* p, const char* p_name);

struct expression_struct* expression_create(
    expression_var_read_lookup_func_t var_read_lookup_func,
    expression_var_write_lookup_func_t var_write_lookup_func,
    void* p_variable_object);
void expression_destroy(struct expression_struct* p_expression);

int64_t expression_parse(struct expression_struct* p_expression,
                         const char* p_expr_str);

const char* expression_get_original_string(
    struct expression_struct* p_expression);
uint32_t expression_get_tree_size(struct expression_struct* p_expression);
int64_t expression_execute(struct expression_struct* p_expression);

#endif /* BEEBJIT_EXPRESSION_H */
