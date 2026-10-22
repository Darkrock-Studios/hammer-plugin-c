// Output for Hammer plugins: a buffer that grows with hammer_must_alloc, and writers for text,
// numbers, and JSON strings. Include it after hammer.h, which HAMMER_IMPLEMENTATION applies to as well.
//
//   static HammerBuffer out;
//   out = (HammerBuffer){0}; // at the start of each export, after hammer_alloc_reset()
//   hammer_put_text(&out, "{\"count\":");
//   hammer_put_int(&out, 12);
//   hammer_put_char(&out, '}');
//   extism_output_buf(out.data, out.length);
//
// A buffer lives in hammer_alloc's memory, so one kept between exports must be zeroed before reuse.
// Growing it moves its data, so hold offsets into it rather than pointers. To undo what was written
// since a mark, set length back to the mark.
#ifndef HAMMER_OUT_H
#define HAMMER_OUT_H

#include "hammer.h"

typedef struct {
  char *data;
  uint64_t length;
  uint64_t capacity;
} HammerBuffer;

void hammer_reserve(HammerBuffer *buffer, uint64_t extra);
void hammer_put(HammerBuffer *buffer, const char *text, uint64_t length);
void hammer_put_text(HammerBuffer *buffer, const char *text);
void hammer_put_char(HammerBuffer *buffer, char c);
void hammer_put_int(HammerBuffer *buffer, int64_t n);
// Appends text as a quoted JSON string, escaping what JSON requires.
void hammer_put_json_string(HammerBuffer *buffer, const char *text, uint64_t length);
// The buffer's text with a 0 after it, which is not counted in its length.
const char *hammer_terminated(HammerBuffer *buffer);

#ifdef HAMMER_IMPLEMENTATION
void hammer_reserve(HammerBuffer *buffer, uint64_t extra) {
  if (buffer->length + extra <= buffer->capacity) return;
  uint64_t capacity = buffer->capacity ? buffer->capacity * 2 : 4096;
  while (capacity < buffer->length + extra) capacity *= 2;
  char *data = hammer_must_alloc(capacity);
  if (buffer->length) memcpy(data, buffer->data, buffer->length);
  buffer->data = data;
  buffer->capacity = capacity;
}

void hammer_put(HammerBuffer *buffer, const char *text, uint64_t length) {
  hammer_reserve(buffer, length);
  memcpy(buffer->data + buffer->length, text, length);
  buffer->length += length;
}

void hammer_put_text(HammerBuffer *buffer, const char *text) { hammer_put(buffer, text, strlen(text)); }

void hammer_put_char(HammerBuffer *buffer, char c) { hammer_put(buffer, &c, 1); }

void hammer_put_int(HammerBuffer *buffer, int64_t n) {
  char digits[20];
  int length = 0;
  uint64_t magnitude = n < 0 ? 0 - (uint64_t)n : (uint64_t)n;
  do {
    digits[length++] = (char)('0' + magnitude % 10);
    magnitude /= 10;
  } while (magnitude);
  if (n < 0) hammer_put_char(buffer, '-');
  while (length) hammer_put_char(buffer, digits[--length]);
}

void hammer_put_json_string(HammerBuffer *buffer, const char *text, uint64_t length) {
  static const char hex[] = "0123456789abcdef";
  hammer_put_char(buffer, '"');
  for (uint64_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)text[i];
    if (c == '"' || c == '\\') {
      hammer_put_char(buffer, '\\');
      hammer_put_char(buffer, (char)c);
    } else if (c < 0x20) {
      hammer_put_text(buffer, "\\u00");
      hammer_put_char(buffer, hex[c >> 4]);
      hammer_put_char(buffer, hex[c & 15]);
    } else {
      hammer_put_char(buffer, (char)c);
    }
  }
  hammer_put_char(buffer, '"');
}

const char *hammer_terminated(HammerBuffer *buffer) {
  hammer_reserve(buffer, 1);
  buffer->data[buffer->length] = 0;
  return buffer->data;
}
#endif // HAMMER_IMPLEMENTATION

#endif // HAMMER_OUT_H
