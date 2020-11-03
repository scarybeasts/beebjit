#include "util.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void (*sighandler_t)(int);

static void (*s_p_interrupt_callback)(void);

void*
util_malloc(size_t size) {
  void* p_ret = malloc(size);
  if (p_ret == NULL) {
    util_bail("malloc failed");
  }

  return p_ret;
}

void*
util_mallocz(size_t size) {
  void* p_ret = util_malloc(size);

  (void) memset(p_ret, '\0', size);

  return p_ret;
}

void
util_free(void* p) {
  free(p);
}

char*
util_strdup(const char* p_str) {
  return strdup(p_str);
}

char*
util_strdup2(const char* p_str1, const char* p_str2) {
  char* p_ret;

  size_t len1 = strlen(p_str1);
  size_t len2 = strlen(p_str2);
  size_t len = (len1 + len2);

  if (((len1 + len2) < len1) || ((len + 1) < len)) {
    util_bail("integer overflow");
  }

  p_ret = malloc(len + 1);
  (void) memcpy(p_ret, p_str1, len1);
  (void) memcpy((p_ret + len1), p_str2, len2);
  p_ret[len] = '\0';

  return p_ret;
}

struct util_buffer {
  uint8_t* p_mem;
  size_t length;
  size_t pos;
  uint8_t* p_base;
};

struct util_buffer*
util_buffer_create() {
  struct util_buffer* p_buf = util_mallocz(sizeof(struct util_buffer));

  return p_buf;
}

void
util_buffer_destroy(struct util_buffer* p_buf) {
  free(p_buf);
}

void
util_buffer_setup(struct util_buffer* p_buf, uint8_t* p_mem, size_t len) {
  p_buf->p_mem = p_mem;
  p_buf->length = len;
  p_buf->pos = 0;
  p_buf->p_base = p_mem;
}

uint8_t*
util_buffer_get_ptr(struct util_buffer* p_buf) {
  return p_buf->p_mem;
}

size_t
util_buffer_get_pos(struct util_buffer* p_buf) {
  return p_buf->pos;
}

void
util_buffer_set_pos(struct util_buffer* p_buf, size_t pos) {
  assert(pos <= p_buf->length);
  p_buf->pos = pos;
}

size_t
util_buffer_remaining(struct util_buffer* p_buf) {
  return (p_buf->length - p_buf->pos);
}

void
util_buffer_append(struct util_buffer* p_buf, struct util_buffer* p_src_buf) {
  assert((p_buf->pos + p_src_buf->pos) >= p_buf->pos);
  assert((p_buf->pos + p_src_buf->pos) <= p_buf->length);

  memcpy((p_buf->p_mem + p_buf->pos), p_src_buf->p_mem, p_src_buf->pos);
  p_buf->pos += p_src_buf->pos;
}

void
util_buffer_set_base_address(struct util_buffer* p_buf, uint8_t* p_base) {
  p_buf->p_base = p_base;
}

uint8_t*
util_buffer_get_base_address(struct util_buffer* p_buf) {
  return p_buf->p_base;
}

void
util_buffer_add_1b(struct util_buffer* p_buf, int b1) {
  assert(b1 >= 0 && b1 <= 0xff);
  assert(p_buf->pos < p_buf->length);
  p_buf->p_mem[p_buf->pos++] = b1;
}

void
util_buffer_add_2b(struct util_buffer* p_buf, int b1, int b2) {
  util_buffer_add_1b(p_buf, b1);
  util_buffer_add_1b(p_buf, b2);
}

void
util_buffer_add_2b_1w(struct util_buffer* p_buf, int b1, int b2, int w1) {
  util_buffer_add_1b(p_buf, b1);
  util_buffer_add_1b(p_buf, b2);
  util_buffer_add_1b(p_buf, w1 & 0xff);
  util_buffer_add_1b(p_buf, w1 >> 8);
}

void
util_buffer_add_3b(struct util_buffer* p_buf, int b1, int b2, int b3) {
  util_buffer_add_1b(p_buf, b1);
  util_buffer_add_1b(p_buf, b2);
  util_buffer_add_1b(p_buf, b3);
}

void
util_buffer_add_4b(struct util_buffer* p_buf, int b1, int b2, int b3, int b4) {
  util_buffer_add_1b(p_buf, b1);
  util_buffer_add_1b(p_buf, b2);
  util_buffer_add_1b(p_buf, b3);
  util_buffer_add_1b(p_buf, b4);
}

void
util_buffer_add_5b(struct util_buffer* p_buf,
                   int b1,
                   int b2,
                   int b3,
                   int b4,
                   int b5) {
  util_buffer_add_1b(p_buf, b1);
  util_buffer_add_1b(p_buf, b2);
  util_buffer_add_1b(p_buf, b3);
  util_buffer_add_1b(p_buf, b4);
  util_buffer_add_1b(p_buf, b5);
}

void
util_buffer_add_int(struct util_buffer* p_buf, int64_t i) {
  int b1;
  int b2;
  int b3;
  int b4;
  assert(i >= INT_MIN);
  assert(i <= INT_MAX);
  b1 = (i & 0xff);
  i >>= 8;
  b2 = (i & 0xff);
  i >>= 8;
  b3 = (i & 0xff);
  i >>= 8;
  b4 = (i & 0xff);

  util_buffer_add_4b(p_buf, b1, b2, b3, b4);
}

void
util_buffer_add_chunk(struct util_buffer* p_buf, void* p_src, size_t size) {
  assert((p_buf->pos + size) >= p_buf->pos);
  assert((p_buf->pos + size) <= p_buf->length);
  (void) memcpy((p_buf->p_mem + p_buf->pos), p_src, size);
  p_buf->pos += size;
}

void
util_buffer_fill_to_end(struct util_buffer* p_buf, char value) {
  util_buffer_fill(p_buf, value, (p_buf->length - p_buf->pos));
}

void
util_buffer_fill(struct util_buffer* p_buf, char value, size_t len) {
  assert((p_buf->pos + len) >= p_buf->pos);
  assert((p_buf->pos + len) <= p_buf->length);
  (void) memset((p_buf->p_mem + p_buf->pos), value, len);
  p_buf->pos += len;
}

int
util_is_extension(const char* p_file_name, const char* p_ext) {
  const char* p_file_name_end;
  const char* p_ext_end;
  size_t i;

  size_t file_name_len = strlen(p_file_name);
  size_t ext_len = strlen(p_ext);

  if (file_name_len < (ext_len + 1)) {
    return 0;
  }

  p_file_name_end = (p_file_name + file_name_len);
  p_ext_end = (p_ext + ext_len);

  i = 0;
  while (i < ext_len) {
    p_file_name_end--;
    p_ext_end--;
    if (*p_ext_end != tolower(*p_file_name_end)) {
      return 0;
    }
    ++i;
  }

  p_file_name_end--;
  if (*p_file_name_end != '.') {
    return 0;
  }

  return 1;
}

struct util_file*
util_file_open(const char* p_file_name, int writeable, int create) {
  FILE* p_file;

  /* Need the "b" aka. "binary" for Windows. */
  const char* p_flags = "rb";

  if (writeable) {
    if (create) {
      p_flags = "wb+";
    } else {
      p_flags = "rb+";
    }
  }

  p_file = fopen(p_file_name, p_flags);
  if (p_file == NULL) {
    util_bail("couldn't open %s", p_file_name);
  }

  return (struct util_file*) p_file;
}

void
util_file_close(struct util_file* p) {
  int ret = fclose((FILE*) p);
  if (ret != 0) {
    util_bail("close failed");
  }
}

uint64_t
util_file_get_pos(struct util_file* p) {
  FILE* p_file = (FILE*) p;

  return ftell(p_file);
}

uint64_t
util_file_get_size(struct util_file* p) {
  int ret;
  uint64_t pos;
  FILE* p_file = (FILE*) p;

  ret = fseek(p_file, 0, SEEK_END);
  if (ret != 0) {
    util_bail("fseek SEEK_END failed");
  }

  pos = ftell(p_file);

  ret = fseek(p_file, 0, SEEK_SET);
  if (ret != 0) {
    util_bail("fseek SEEK_SET failed");
  }

  return pos;
}

uint64_t
util_file_read(struct util_file* p, void* p_buf, uint64_t length) {
  FILE* p_file = (FILE*) p;

  return fread(p_buf, 1, length, p_file);
}

void
util_file_write(struct util_file* p, const void* p_buf, uint64_t length) {
  FILE* p_file = (FILE*) p;
  uint64_t ret = fwrite(p_buf, 1, length, p_file);
  if (ret != length) {
    util_bail("fwrite short write");
  }
}

void
util_file_seek(struct util_file* p, uint64_t pos) {
  int ret;
  FILE* p_file = (FILE*) p;

  ret = fseek(p_file, pos, SEEK_SET);
  if (ret != 0) {
    util_bail("fseek SEEK_SET failed");
  }
}

void
util_file_flush(struct util_file* p) {
  FILE* p_file = (FILE*) p;

  int ret = fflush(p_file);
  if (ret != 0) {
    util_bail("fflush failed");
  }
}

uint64_t
util_file_read_fully(const char* p_file_name,
                     uint8_t* p_buf,
                     uint64_t max_size) {
  uint64_t ret;
  struct util_file* p_file = util_file_open(p_file_name, 0, 0);

  ret = util_file_read(p_file, p_buf, max_size);

  util_file_close(p_file);

  return ret;
}

void
util_file_write_fully(const char* p_file_name,
                      const uint8_t* p_buf,
                      uint64_t size) {
  struct util_file* p_file = util_file_open(p_file_name, 1, 1);
  util_file_write(p_file, p_buf, size);
  util_file_close(p_file);
}

void
util_file_copy(const char* p_src_file_name, const char* p_dst_file_name) {
  char buf[4096];
  uint64_t to_go;

  struct util_file* p_src_file = util_file_open(p_src_file_name, 0, 0);
  struct util_file* p_dst_file = util_file_open(p_dst_file_name, 1, 1);

  to_go = util_file_get_size(p_src_file);
  while (to_go > 0) {
    uint64_t ret;
    uint64_t length = sizeof(buf);
    if (to_go < length) {
      length = to_go;
    }
    ret = util_file_read(p_src_file, buf, length);
    if (ret != length) {
      util_bail("util_file_read short read");
    }
    util_file_write(p_dst_file, buf, length);

    to_go -= length;
  }

  util_file_close(p_src_file);
  util_file_close(p_dst_file);
}

intptr_t
util_get_stdin_handle() {
  return fileno(stdin);
}

intptr_t
util_get_stdout_handle() {
  return fileno(stdout);
}

uint8_t
util_handle_read_byte(intptr_t handle) {
  uint8_t val;

  ssize_t ret = read(handle, &val, 1);
  if (ret != 1) {
    util_bail("failed to read byte from handle");
  }

  return val;
}

void
util_handle_write_byte(intptr_t handle, uint8_t val) {
  ssize_t ret = write(handle, &val, 1);
  if (ret != 1) {
    util_bail("failed to write byte to handle");
  }
}

void
util_handle_close(intptr_t handle) {
  int fd = (int) handle;
  int ret = close(fd);
  if (ret != 0) {
    util_bail("close failed");
  }
}

static const char*
util_locate_option(const char* p_opt_str,
                   const char* p_opt_name) {
  const char* p_opt_pos;

  if (p_opt_str == NULL) {
    return NULL;
  }

  p_opt_pos = strstr(p_opt_str, p_opt_name);
  if (p_opt_pos != NULL) {
    p_opt_pos += strlen(p_opt_name);
  }

  return p_opt_pos;
}

int
util_get_u32_option(uint32_t* p_opt_out,
                    const char* p_opt_str,
                    const char* p_opt_name) {
  int matches;

  const char* p_opt_pos = util_locate_option(p_opt_str, p_opt_name);
  if (p_opt_pos == NULL) {
    return 0;
  }

  matches = sscanf(p_opt_pos, "%"PRIu32, p_opt_out);
  if (matches != 1) {
    return 0;
  }
  return 1;
}

int
util_get_u64_option(uint64_t* p_opt_out,
                    const char* p_opt_str,
                    const char* p_opt_name) {
  int matches;

  const char* p_opt_pos = util_locate_option(p_opt_str, p_opt_name);
  if (p_opt_pos == NULL) {
    return 0;
  }

  matches = sscanf(p_opt_pos, "%"PRIu64, p_opt_out);
  if (matches != 1) {
    return 0;
  }
  return 1;
}

int
util_get_str_option(char** p_opt_out,
                    const char* p_opt_str,
                    const char* p_opt_name) {
  size_t len;
  const char* p_opt_end;
  char* p_ret;

  const char* p_opt_pos = util_locate_option(p_opt_str, p_opt_name);
  if (p_opt_pos == NULL) {
    return 0;
  }

  p_opt_end = p_opt_pos;
  while (*p_opt_end != '\0' && *p_opt_end != ',') {
    p_opt_end++;
  }

  /* NOTE: would use strndup() here but Windows doesn't have it verbatim. */
  len = (p_opt_end - p_opt_pos);
  p_ret = malloc(len + 1);
  (void) memcpy(p_ret, p_opt_pos, len);
  p_ret[len] = '\0';
  *p_opt_out = p_ret;

  return 1;
}

int
util_has_option(const char* p_opt_str, const char* p_opt_name) {
  if (strstr(p_opt_str, p_opt_name)) {
    return 1;
  }
  return 0;
}

void
util_bail(const char* p_msg, ...) {
  va_list args;
  char msg[256];

  va_start(args, p_msg);
  msg[0] = '\0';
  (void) vsnprintf(msg, sizeof(msg), p_msg, args);
  va_end(args);

  (void) fprintf(stderr, "BAILING: %s\n", msg);

  exit(1);
  /* Not reached. */
}

static void
sigint_handler(int signum) {
  if (signum != SIGINT) {
    _exit(1);
  }

  s_p_interrupt_callback();
}

void
util_set_interrupt_callback(void (*p_interrupt_callback)(void)) {
  sighandler_t ret;

  s_p_interrupt_callback = p_interrupt_callback;

  ret = signal(SIGINT, sigint_handler);
  if (ret == SIG_ERR) {
    util_bail("signal failed");
  }
}

static uint8_t
util_hex_char_to_val(char hex_char) {
  int upper_char = toupper(hex_char);
  uint8_t val = 0;

  if ((upper_char >= '0') && (upper_char <= '9')) {
    val = (upper_char - '0');
  } else if ((upper_char >= 'A') && (upper_char <= 'F')) {
    val = (upper_char - 'A' + 10);
  }

  return val;
}

uint8_t
util_parse_hex2(const char* p_str) {
  uint8_t val;

  assert((*p_str != '\0') && (*(p_str + 1) != '\0'));

  val = util_hex_char_to_val(p_str[0]);
  val <<= 4;
  val |= util_hex_char_to_val(p_str[1]);

  return val;
}
