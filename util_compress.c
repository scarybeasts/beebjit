#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_STDIO

#include "miniz.c"

#include "util_compress.h"

#include <string.h>

static int
util_gunzip_chunk(size_t* p_dst_len,
                  size_t* p_src_consumed,
                  uint8_t* p_src,
                  size_t src_len,
                  uint8_t* p_dst) {
  z_stream stream;
  int status;
  uint8_t flags;
  size_t dst_len = *p_dst_len;
  size_t src_left = src_len;

  if (src_left < 10) {
    return -100;
  }
  if ((p_src[0] != 0x1F) || (p_src[1] != 0x8B)) {
    return -101;
  }
  /* Method needs to be deflate. */
  if (p_src[2] != 0x08) {
    return -102;
  }
  /* No support for most extra flags etc. */
  flags = p_src[3];
  if ((flags & ~0x08) != 0x00) {
    return -103;
  }

  /* Skip header. */
  p_src += 10;
  src_left -= 10;

  /* Skip filename if any. */
  if (flags & 0x08) {
    while ((src_left > 0) && (*p_src != '\0')) {
      p_src++;
      src_left--;
    }
    if (src_left > 0) {
      p_src++;
      src_left--;
    }
  }

  (void) memset(&stream, '\0', sizeof(stream));
  stream.next_in = p_src;
  stream.avail_in = src_left;
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
  src_left -= stream.total_in;
  *p_src_consumed = (src_len - src_left);

  if (inflateEnd(&stream) != Z_OK) {
    return -3;
  }

  return 0;
}

int
util_gunzip(size_t* p_dst_len, uint8_t* p_src, size_t src_len, uint8_t* p_dst) {
  size_t src_left = src_len;
  size_t dst_left = *p_dst_len;
  size_t dst_written = 0;
  int ret;

  while (1) {
    size_t src_consumed = 0;
    size_t dst_len = dst_left;
    ret = util_gunzip_chunk(&dst_len, &src_consumed, p_src, src_left, p_dst);
    if (ret != 0) {
      break;
    }

    dst_written += dst_len;
    dst_left -= dst_len;
    p_dst += dst_len;
    src_left -= src_consumed;
    p_src += src_consumed;

    /* Go again if there are more gzip headers in the expected place. BeebEm
     * seems to emit UEF files that are multiple concatenated gzip files.
     */
    if ((src_left > 10) && (p_src[8] == 0x1f) && (p_src[9] == 0x8b)) {
      src_left -= 8;
      p_src += 8;
      /* Go again. */
    } else {
      break;
    }
  }

  *p_dst_len = dst_written;
  return ret;
}

int
util_uncompress(size_t* p_dst_len,
                uint8_t* p_src,
                size_t src_len,
                uint8_t* p_dst) {
  mz_ulong dst_len = *p_dst_len;
  int ret = uncompress(p_dst, &dst_len, p_src, src_len);

  if (ret != Z_OK) {
    return -1;
  }

  *p_dst_len = dst_len;


  return 0;
}
