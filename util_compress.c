#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_STDIO

#include "miniz.c"

#include "util_compress.h"

#include <string.h>

int
util_gunzip(size_t* p_dst_len, uint8_t* p_src, size_t src_len, uint8_t* p_dst) {
  z_stream stream;
  int status;
  size_t dst_len = *p_dst_len;

  if (src_len < 10) {
    return -100;
  }
  if ((p_src[0] != 0x1F) || (p_src[1] != 0x8B)) {
    return -101;
  }
  /* Method needs to be deflate. */
  if (p_src[2] != 0x08) {
    return -102;
  }
  /* No support for extra flags etc. */
  if (p_src[3] != 0x00) {
    return -103;
  }

  p_src += 10;
  src_len -= 10;

  (void) memset(&stream, '\0', sizeof(stream));
  stream.next_in = p_src;
  stream.avail_in = src_len;
  stream.next_out = p_dst;
  stream.avail_out = dst_len;

  if (inflateInit2(&stream, -Z_DEFAULT_WINDOW_BITS) != Z_OK) {
    return -1;
  }

  status = inflate(&stream, Z_FINISH);

  if (status != Z_STREAM_END) {
    return -2;
  }

  *p_dst_len = (dst_len - stream.avail_out);

  if (inflateEnd(&stream) != Z_OK) {
    return -3;
  }

  return 0;
}
