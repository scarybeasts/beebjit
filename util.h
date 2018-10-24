#ifndef BEEBJIT_UTIL_H
#define BEEBJIT_UTIL_H

#include <stdint.h>
#include <unistd.h>

/* Memory mapping. */
int util_get_memory_fd(size_t size);
void* util_get_guarded_mapping(void* p_addr, size_t size);
void* util_get_guarded_mapping_from_fd(int fd, void* p_addr, size_t size);
void util_free_guarded_mapping(void* p_addr, size_t size);
void util_make_mapping_read_only(void* p_addr, size_t size);
void util_make_mapping_read_write(void* p_addr, size_t size);
void util_make_mapping_read_write_exec(void* p_addr, size_t size);
void util_make_mapping_none(void* p_addr, size_t size);
void* util_get_fixed_anonymous_mapping(void* p_addr, size_t size);
void* util_get_fixed_mapping_from_fd(int fd, void* p_addr, size_t size);

/* Buffer. */
struct util_buffer;
struct util_buffer* util_buffer_create();
void util_buffer_destroy(struct util_buffer* p_buf);
void util_buffer_setup(struct util_buffer* p_buf,
                       unsigned char* p_mem,
                       size_t len);
unsigned char* util_buffer_get_ptr(struct util_buffer* p_buf);
size_t util_buffer_get_pos(struct util_buffer* p_buf);
void util_buffer_set_pos(struct util_buffer* p_buf, size_t len);
size_t util_buffer_remaining(struct util_buffer* p_buf);
void util_buffer_append(struct util_buffer* p_buf,
                        struct util_buffer* p_src_buf);
void util_buffer_set_base_address(struct util_buffer* p_buf,
                                  unsigned char* p_base);
unsigned char* util_buffer_get_base_address(struct util_buffer* p_buf);

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
void util_buffer_add_int(struct util_buffer* p_buf, ssize_t i);
void util_buffer_add_chunk(struct util_buffer* p_buf, void* p_src, size_t size);

/* File. */
size_t util_file_read(unsigned char* p_buf,
                      size_t max_size,
                      const char* p_file_name);
void util_file_write(const char* p_file_name,
                     const unsigned char* p_buf,
                     size_t size);

/* Timing. */
/* These quantities are in microseconds. */
uint64_t util_gettime();
void util_sleep_until(uint64_t time);

/* Channels. */
void util_get_channel_fds(int* fd1, int* fd2);

/* Options. */
int util_get_int_option(int* p_opt_out,
                        const char* p_opt_str,
                        const char* p_opt_name);

#endif /* BEEBJIT_UTIL_H */
