// Image references in Markdown, needing no allocation. Hammer shows a project image as
// `![image](hammer://image/<id>)`, or with the destination in angle brackets, `<hammer://image/<id>>`;
// the alt text means nothing. Get an image's bytes with hammer_call's image.get, by its id.
//
//   uint64_t at = 0;
//   int32_t id;
//   while ((id = hammer_next_image_id(markdown, &at)) >= 0) ...
#ifndef HAMMER_IMAGE_H
#define HAMMER_IMAGE_H

#include "hammer_text.h"

#define HAMMER_IMAGE_PREFIX "hammer://image/"
#define HAMMER_IMAGE_PREFIX_LENGTH (sizeof HAMMER_IMAGE_PREFIX - 1)

static inline int hammer_image_word_byte(unsigned char c) {
  return hammer_is_ascii_letter((char)c) || hammer_is_digit((char)c) || c == '_';
}

// Whether `hammer://image/` starts at byte i; freestanding builds have no memcmp.
static inline int hammer_image_prefix_at(HammerText text, uint64_t i) {
  if (i > text.length || text.length - i < HAMMER_IMAGE_PREFIX_LENGTH) return 0;
  for (uint64_t k = 0; k < HAMMER_IMAGE_PREFIX_LENGTH; k++) {
    if (text.data[i + k] != HAMMER_IMAGE_PREFIX[k]) return 0;
  }
  return 1;
}

// The id in the digits [start, end) of a text, or -1 when they are none or too many for an int32_t.
static inline int32_t hammer_image_digits(HammerText text, uint64_t start, uint64_t end) {
  if (end == start) return -1;
  int64_t id = 0;
  for (uint64_t i = start; i < end; i++) {
    id = id * 10 + (text.data[i] - '0');
    if (id > INT32_MAX) return -1;
  }
  return (int32_t)id;
}

// The image id a reference names: `hammer://image/3`, or `<hammer://image/3>`, is 3. -1 for anything
// else, such as another scheme, an id that is empty, not all ASCII digits, or too large for an int32_t.
static inline int32_t hammer_image_id(HammerText reference) {
  uint64_t start = 0, end = reference.length;
  if (end >= 2 && reference.data[0] == '<' && reference.data[end - 1] == '>') {
    start++;
    end--;
  }
  if (end - start < HAMMER_IMAGE_PREFIX_LENGTH || !hammer_image_prefix_at(reference, start)) return -1;
  start += HAMMER_IMAGE_PREFIX_LENGTH;
  for (uint64_t i = start; i < end; i++) {
    if (!hammer_is_digit(reference.data[i])) return -1;
  }
  return hammer_image_digits(reference, start, end);
}

// The next image id a Markdown text refers to from byte *at on, moving *at past it; -1 when there are
// no more. It finds them in images, angle-bracket destinations, reference definitions
// (`[map]: hammer://image/3`), and code alike, as Hammer counts them, in order and once for each time
// they appear. A reference is `hammer://image/` and its digits where no letter, digit, `_`, `+`, `.`,
// or `-` comes just before it and no letter, digit, or `_` just after, so `hammer://image/12x` names
// none. An id too large for an int32_t names none either.
static inline int32_t hammer_next_image_id(HammerText text, uint64_t *at) {
  uint64_t i = *at < text.length ? *at : text.length;
  while (i + HAMMER_IMAGE_PREFIX_LENGTH <= text.length) {
    if (!hammer_image_prefix_at(text, i)) {
      i++;
      continue;
    }
    unsigned char before = i > 0 ? (unsigned char)text.data[i - 1] : 0;
    uint64_t start = i + HAMMER_IMAGE_PREFIX_LENGTH, end = start;
    while (end < text.length && hammer_is_digit(text.data[end])) end++;
    i = end;
    if (hammer_image_word_byte(before) || before == '+' || before == '.' || before == '-') continue;
    if (hammer_image_word_byte(hammer_byte(text, end))) continue;
    int32_t id = hammer_image_digits(text, start, end);
    if (id >= 0) {
      *at = end;
      return id;
    }
  }
  *at = text.length;
  return -1;
}

#endif // HAMMER_IMAGE_H
