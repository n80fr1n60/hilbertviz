#include "file_io.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(__GNUC__) || defined(__clang__)
#define HV_PRINTF_LIKE(fmt_idx, first_arg_idx) __attribute__((format(printf, fmt_idx, first_arg_idx)))
#else
#define HV_PRINTF_LIKE(fmt_idx, first_arg_idx)
#endif

static void HV_PRINTF_LIKE(3, 4) hv_set_error(char *err, size_t err_size, const char *fmt, ...)
{
  va_list args;

  if ((err == 0) || (err_size == 0u)) {
    return;
  }

  va_start(args, fmt);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  (void)vsnprintf(err, err_size, fmt, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  va_end(args);
}

static int hv_add_u64(uint64_t a, uint64_t b, uint64_t *out)
{
  if (out == 0) {
    return 0;
  }
  if (b > (UINT64_MAX - a)) {
    return 0;
  }
  *out = a + b;
  return 1;
}

static int hv_validate_slice_from_file(
  FILE *fp,
  const char *path,
  uint64_t offset,
  int has_length,
  uint64_t length,
  uint64_t *slice_len_out,
  char *err,
  size_t err_size
)
{
  struct stat st;
  uint64_t file_size = 0;
  int fd = -1;
  const char *path_text = (path != 0) ? path : "(input)";

  if ((fp == 0) || (slice_len_out == 0)) {
    hv_set_error(err, err_size, "invalid arguments for slice validation");
    return 0;
  }

  fd = fileno(fp);
  if (fd < 0) {
    hv_set_error(err, err_size, "failed to access file descriptor for '%s'", path_text);
    return 0;
  }

  if (fstat(fd, &st) != 0) {
    hv_set_error(err, err_size, "failed to stat opened input '%s': %s", path_text, strerror(errno));
    return 0;
  }

  if (st.st_size < 0) {
    hv_set_error(err, err_size, "negative file size reported for '%s'", path_text);
    return 0;
  }
  file_size = (uint64_t)st.st_size;

  if (offset > file_size) {
    hv_set_error(
      err,
      err_size,
      "offset (%" PRIu64 ") exceeds file size (%" PRIu64 ")",
      offset,
      file_size
    );
    return 0;
  }

  if (has_length) {
    uint64_t end = 0;
    if (!hv_add_u64(offset, length, &end) || (end > file_size)) {
      hv_set_error(
        err,
        err_size,
        "slice [offset=%" PRIu64 ", length=%" PRIu64 "] is outside file size (%" PRIu64 ")",
        offset,
        length,
        file_size
      );
      return 0;
    }
    *slice_len_out = length;
  } else {
    *slice_len_out = file_size - offset;
  }

  return 1;
}

static int hv_seek_to_offset(FILE *fp, uint64_t offset, char *err, size_t err_size)
{
  if (fp == 0) {
    hv_set_error(err, err_size, "invalid file handle");
    return 0;
  }

  if (offset == 0u) {
    return 1;
  }

  if (offset > (uint64_t)INT64_MAX) {
    hv_set_error(err, err_size, "offset is too large for fseeko");
    return 0;
  }

  if (fseeko(fp, (off_t)offset, SEEK_SET) != 0) {
    hv_set_error(err, err_size, "failed to seek input: %s", strerror(errno));
    return 0;
  }

  return 1;
}

void hv_free_buffer(HvBuffer *buffer)
{
  if (buffer == 0) {
    return;
  }
  free(buffer->data);
  buffer->data = 0;
  buffer->size = 0u;
}

int hv_read_file_slice(
  const char *path,
  uint64_t offset,
  int has_length,
  uint64_t length,
  HvBuffer *out,
  char *err,
  size_t err_size
)
{
  uint64_t slice_len = 0;
  size_t target_size = 0;
  FILE *fp = 0;
  uint8_t *buf = 0;
  size_t total = 0;

  if ((path == 0) || (out == 0)) {
    hv_set_error(err, err_size, "invalid arguments for file read");
    return 0;
  }

  out->data = 0;
  out->size = 0u;

  fp = fopen(path, "rb");
  if (fp == 0) {
    hv_set_error(err, err_size, "failed to open input '%s': %s", path, strerror(errno));
    return 0;
  }

  if (!hv_validate_slice_from_file(fp, path, offset, has_length, length, &slice_len, err, err_size)) {
    (void)fclose(fp);
    return 0;
  }

  if (slice_len > (uint64_t)SIZE_MAX) {
    hv_set_error(err, err_size, "slice too large for host size_t");
    (void)fclose(fp);
    return 0;
  }
  target_size = (size_t)slice_len;

  if (!hv_seek_to_offset(fp, offset, err, err_size)) {
    (void)fclose(fp);
    return 0;
  }

  if (target_size == 0u) {
    if (fclose(fp) != 0) {
      hv_set_error(err, err_size, "failed to close input '%s': %s", path, strerror(errno));
      return 0;
    }
    return 1;
  }

  buf = (uint8_t *)malloc(target_size);
  if (buf == 0) {
    hv_set_error(err, err_size, "failed to allocate %zu bytes", target_size);
    (void)fclose(fp);
    return 0;
  }

  while (total < target_size) {
    size_t remaining = target_size - total;
    size_t chunk = remaining;
    size_t n = 0;

    if (chunk > (size_t)(1u << 20u)) {
      chunk = (size_t)(1u << 20u);
    }

    n = fread(buf + total, 1u, chunk, fp);
    if (n == 0u) {
      if (ferror(fp) != 0) {
        hv_set_error(err, err_size, "failed while reading '%s'", path);
        free(buf);
        (void)fclose(fp);
        return 0;
      }
      hv_set_error(
        err,
        err_size,
        "unexpected EOF: read %zu bytes, expected %zu bytes from '%s'",
        total,
        target_size,
        path
      );
      free(buf);
      (void)fclose(fp);
      return 0;
    }
    total += n;
    if (n < chunk) {
      if (ferror(fp) != 0) {
        hv_set_error(err, err_size, "failed while reading '%s'", path);
      } else {
        hv_set_error(
          err,
          err_size,
          "unexpected EOF: read %zu bytes, expected %zu bytes from '%s'",
          total,
          target_size,
          path
        );
      }
      free(buf);
      (void)fclose(fp);
      return 0;
    }
  }

  if (fclose(fp) != 0) {
    hv_set_error(err, err_size, "failed to close input '%s': %s", path, strerror(errno));
    free(buf);
    return 0;
  }

  out->data = buf;
  out->size = target_size;
  return 1;
}

int hv_open_file_slice_stream(
  const char *path,
  uint64_t offset,
  int has_length,
  uint64_t length,
  HvInputStream *stream,
  char *err,
  size_t err_size
)
{
  uint64_t slice_len = 0u;
  FILE *fp = 0;

  if ((path == 0) || (stream == 0)) {
    hv_set_error(err, err_size, "invalid arguments for stream open");
    return 0;
  }

  stream->fp = 0;
  stream->remaining = 0u;
  stream->total = 0u;

  fp = fopen(path, "rb");
  if (fp == 0) {
    hv_set_error(err, err_size, "failed to open input '%s': %s", path, strerror(errno));
    return 0;
  }

  if (!hv_validate_slice_from_file(fp, path, offset, has_length, length, &slice_len, err, err_size)) {
    (void)fclose(fp);
    return 0;
  }

  if (!hv_seek_to_offset(fp, offset, err, err_size)) {
    (void)fclose(fp);
    return 0;
  }

  stream->fp = fp;
  stream->remaining = slice_len;
  stream->total = slice_len;
  return 1;
}

int hv_stream_read_exact(
  HvInputStream *stream,
  uint8_t *buf,
  size_t size,
  char *err,
  size_t err_size
)
{
  size_t total = 0u;

  if ((stream == 0) || (stream->fp == 0) || ((size > 0u) && (buf == 0))) {
    hv_set_error(err, err_size, "invalid arguments for stream read");
    return 0;
  }

  if ((uint64_t)size > stream->remaining) {
    hv_set_error(
      err,
      err_size,
      "stream read exceeds remaining slice (requested=%zu, remaining=%" PRIu64 ")",
      size,
      stream->remaining
    );
    return 0;
  }

  while (total < size) {
    size_t request = size - total;
    size_t n = fread(buf + total, 1u, request, stream->fp);
    if (n == 0u) {
      if (ferror(stream->fp) != 0) {
        hv_set_error(err, err_size, "failed while reading input stream");
        return 0;
      }
      hv_set_error(err, err_size, "unexpected EOF while reading input stream");
      return 0;
    }
    total += n;
    if (n < request) {
      if (ferror(stream->fp) != 0) {
        hv_set_error(err, err_size, "failed while reading input stream");
      } else {
        hv_set_error(err, err_size, "unexpected EOF while reading input stream");
      }
      return 0;
    }
  }

  stream->remaining -= (uint64_t)size;
  return 1;
}

int hv_close_input_stream(HvInputStream *stream, char *err, size_t err_size)
{
  int rc = 0;

  if (stream == 0) {
    hv_set_error(err, err_size, "invalid arguments for stream close");
    return 0;
  }

  if (stream->fp == 0) {
    stream->remaining = 0u;
    stream->total = 0u;
    return 1;
  }

  rc = fclose(stream->fp);
  stream->fp = 0;
  stream->remaining = 0u;
  stream->total = 0u;

  if (rc != 0) {
    hv_set_error(err, err_size, "failed to close input stream: %s", strerror(errno));
    return 0;
  }

  return 1;
}
