#ifndef BEEBJIT_UTIL_H
#define BEEBJIT_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Memory. */
void* util_malloc(size_t size);
void* util_mallocz(size_t size);
void util_free(void* p);
char* util_strdup(const char* p_str);
char* util_strdup2(const char* p_str1, const char* p_str2);

/* Buffer. */
struct util_buffer;
struct util_buffer* util_buffer_create();
void util_buffer_destroy(struct util_buffer* p_buf);
void util_buffer_setup(struct util_buffer* p_buf, uint8_t* p_mem, size_t len);
uint8_t* util_buffer_get_ptr(struct util_buffer* p_buf);
size_t util_buffer_get_pos(struct util_buffer* p_buf);
void util_buffer_set_pos(struct util_buffer* p_buf, size_t len);
size_t util_buffer_remaining(struct util_buffer* p_buf);
void util_buffer_append(struct util_buffer* p_buf,
                        struct util_buffer* p_src_buf);
void util_buffer_set_base_address(struct util_buffer* p_buf, uint8_t* p_base);
uint8_t* util_buffer_get_base_address(struct util_buffer* p_buf);

void util_buffer_add_1b(struct util_buffer* p_buf, int b1);
void util_buffer_add_2b(struct util_buffer* p_buf, int b1, int b2);
void util_buffer_add_2b_1w(struct util_buffer* p_buf, int b1, int b2, int w1);
void util_buffer_add_3b(struct util_buffer* p_buf, int b1, int b2, int b3);
void util_buffer_add_4b(struct util_buffer* p_buf,
                        int b1,
                        int b2,
                        int b3,
                        int b4);
void util_buffer_add_5b(struct util_buffer* p_buf,
                        int b1,
                        int b2,
                        int b3,
                        int b4,
                        int b5);
void util_buffer_add_int(struct util_buffer* p_buf, int64_t i);
void util_buffer_add_chunk(struct util_buffer* p_buf, void* p_src, size_t size);
void util_buffer_fill_to_end(struct util_buffer* p_buf, char value);
void util_buffer_fill(struct util_buffer* p_buf, char value, size_t len);

/* File. */
struct util_file;

int util_is_extension(const char* p_name, const char* p_ext);
enum {
  k_util_file_no_handle = -1,
};

struct util_file* util_file_open(const char* p_file_name,
                                 int writeable,
                                 int create);
void util_file_close(struct util_file* p_file);
uint64_t util_file_get_pos(struct util_file* p_file);
uint64_t util_file_get_size(struct util_file* p_file);
void util_file_seek(struct util_file* p_file, uint64_t pos);
uint64_t util_file_read(struct util_file* p_file, void* p_buf, uint64_t length);
void util_file_write(struct util_file* p_file,
                     const void* p_buf,
                     uint64_t length);
void util_file_flush(struct util_file* p_file);
int util_file_feof(struct util_file* p_file);

uint64_t util_file_read_fully(const char* p_file_name,
                              uint8_t* p_buf,
                              uint64_t max_size);
void util_file_write_fully(const char* p_file_name,
                           const uint8_t* p_buf,
                           uint64_t size);
void util_file_copy(const char* p_src_file_name, const char* p_dst_file_name);

/* Miscellaneous handle I/O. */
intptr_t util_get_stdin_handle();
intptr_t util_get_stdout_handle();
uint8_t util_handle_read_byte(intptr_t handle);
void util_handle_write_byte(intptr_t handle, uint8_t val);
void util_handle_close(intptr_t handle);

/* Options. */
int util_get_u32_option(uint32_t* p_opt_out,
                        const char* p_opt_str,
                        const char* p_opt_name);
int util_get_u64_option(uint64_t* p_opt_out,
                        const char* p_opt_str,
                        const char* p_opt_name);
int util_get_str_option(char** p_opt_out,
                        const char* p_opt_str,
                        const char* p_opt_name);
int util_has_option(const char* p_opt_str, const char* p_opt_name);

/* Misc. */
void util_bail(const char* p_msg, ...) __attribute__((format(printf, 1, 2)));
void util_set_interrupt_callback(void (*p_interrupt_callback)(void));
uint8_t util_parse_hex2(const char* p_str);

#endif /* BEEBJIT_UTIL_H */
