// Text for Hammer plugins: UTF-8 stepping, letters, and words, with offsets in bytes as Hammer's
// diagnostics want them.
//
//   HammerText text = {paragraph, length};
//   uint64_t count;
//   HammerWord *words = hammer_words(text, &count);
//   if (count > 1 && hammer_same_word(text, words[0], words[1])) ...
//
// Letters are told apart roughly outside ASCII: anything but Latin-1 symbols and general punctuation
// (quotes, dashes, the ellipsis) counts as one. Case is known for ASCII, Latin-1, Latin Extended-A, and
// Cyrillic; other letters have none.
#ifndef HAMMER_TEXT_H
#define HAMMER_TEXT_H

#include "hammer.h"

typedef struct {
  const char *data;
  uint64_t length;
} HammerText;

// Bytes [start, end) of a text.
typedef struct {
  uint64_t start;
  uint64_t end;
} HammerWord;

static inline int hammer_is_lower(char c) { return c >= 'a' && c <= 'z'; }
static inline int hammer_is_upper(char c) { return c >= 'A' && c <= 'Z'; }
static inline int hammer_is_ascii_letter(char c) { return hammer_is_lower(c) || hammer_is_upper(c); }
static inline int hammer_is_digit(char c) { return c >= '0' && c <= '9'; }
static inline char hammer_to_lower(char c) { return hammer_is_upper(c) ? (char)(c + 32) : c; }
static inline char hammer_to_upper(char c) { return hammer_is_lower(c) ? (char)(c - 32) : c; }

// The code point in the first `available` bytes of s, and in *length its bytes; U+FFFD, a byte long,
// for a continuation byte or a character cut short.
static inline uint32_t hammer_decode(const char *s, uint64_t available, uint64_t *length) {
  unsigned char c = (unsigned char)s[0];
  uint64_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
  *length = n > available ? 1 : n;
  if (c < 0x80) return c;
  if (n == 1 || n > available) return 0xFFFD;
  uint32_t code = c & (0x7F >> n);
  for (uint64_t i = 1; i < n; i++) code = code << 6 | ((unsigned char)s[i] & 0x3F);
  return code;
}

// Writes a code point's UTF-8 to out, which has room for 4 bytes; returns how many it wrote.
static inline uint64_t hammer_encode(uint32_t code, char *out) {
  if (code < 0x80) {
    out[0] = (char)code;
    return 1;
  }
  if (code < 0x800) {
    out[0] = (char)(0xC0 | code >> 6);
    out[1] = (char)(0x80 | (code & 0x3F));
    return 2;
  }
  if (code < 0x10000) {
    out[0] = (char)(0xE0 | code >> 12);
    out[1] = (char)(0x80 | ((code >> 6) & 0x3F));
    out[2] = (char)(0x80 | (code & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | code >> 18);
  out[1] = (char)(0x80 | ((code >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((code >> 6) & 0x3F));
  out[3] = (char)(0x80 | (code & 0x3F));
  return 4;
}

// Latin Extended-A pairs capitals with the small letters after them, but from U+0139 to U+0148 and
// U+0179 to U+017E a capital sits at each odd code point; 0 for its caseless letters.
static inline int hammer_latin_a_capital(uint32_t code) {
  if (code == 0x130 || code == 0x131 || code == 0x138 || code == 0x149 || code == 0x17F) return 0;
  int odd = (code >= 0x139 && code <= 0x148) || (code >= 0x179 && code <= 0x17E);
  return (int)(code % 2) == odd ? 1 : -1;
}

static inline uint32_t hammer_lower(uint32_t code) {
  if (code < 0x80) return hammer_is_upper((char)code) ? code + 32 : code;
  if (code >= 0xC0 && code <= 0xDE && code != 0xD7) return code + 32;
  if (code == 0x178) return 0xFF;
  if (code >= 0x100 && code <= 0x17F) return hammer_latin_a_capital(code) == 1 ? code + 1 : code;
  if (code >= 0x410 && code <= 0x42F) return code + 32;
  if (code >= 0x400 && code <= 0x40F) return code + 80;
  if (code >= 0x490 && code <= 0x4BF) return code | 1;
  return code;
}

static inline uint32_t hammer_upper(uint32_t code) {
  if (code < 0x80) return hammer_is_lower((char)code) ? code - 32 : code;
  if (code >= 0xE0 && code <= 0xFE && code != 0xF7) return code - 32;
  if (code == 0xFF) return 0x178;
  if (code >= 0x100 && code <= 0x17F) return hammer_latin_a_capital(code) == -1 ? code - 1 : code;
  if (code >= 0x430 && code <= 0x44F) return code - 32;
  if (code >= 0x450 && code <= 0x45F) return code - 80;
  if (code >= 0x490 && code <= 0x4BF) return code & ~1u;
  return code;
}

// The byte at i, or 0 past the end.
static inline unsigned char hammer_byte(HammerText text, uint64_t i) {
  return i < text.length ? (unsigned char)text.data[i] : 0;
}

// Whether the three bytes at i are the UTF-8 for code point `code`, from U+0800 to U+FFFF.
static inline int hammer_char_is(HammerText text, uint64_t i, uint32_t code) {
  return hammer_byte(text, i) == (0xE0 | (code >> 12)) && hammer_byte(text, i + 1) == (0x80 | ((code >> 6) & 0x3F)) &&
         hammer_byte(text, i + 2) == (0x80 | (code & 0x3F));
}

// The code point at i, and in *length its bytes; 0 past the end.
static inline uint32_t hammer_char_at(HammerText text, uint64_t i, uint64_t *length) {
  if (i >= text.length) {
    *length = 0;
    return 0;
  }
  return hammer_decode(text.data + i, text.length - i, length);
}

// Whether the character at i is a capital, or a small letter with a capital.
static inline int hammer_upper_at(HammerText text, uint64_t i) {
  uint64_t length;
  uint32_t code = hammer_char_at(text, i, &length);
  return hammer_lower(code) != code;
}

static inline int hammer_lower_at(HammerText text, uint64_t i) {
  uint64_t length;
  uint32_t code = hammer_char_at(text, i, &length);
  return hammer_upper(code) != code;
}

// Where the character before i, which is more than 0, starts.
static inline uint64_t hammer_char_before(HammerText text, uint64_t i) {
  uint64_t start = i - 1;
  while (start > 0 && i - start < 4 && (hammer_byte(text, start) & 0xC0) == 0x80) start--;
  return start;
}

// Bytes in the character at i: 1 for ASCII and stray continuation bytes.
static inline uint64_t hammer_char_length(HammerText text, uint64_t i) {
  unsigned char c = hammer_byte(text, i);
  if (c >= 0xF0) return 4;
  if (c >= 0xE0) return 3;
  if (c >= 0xC0) return 2;
  return 1;
}

static inline int hammer_letter_at(HammerText text, uint64_t i) {
  unsigned char c = hammer_byte(text, i);
  if (c < 0x80) return hammer_is_ascii_letter((char)c);
  if (c == 0xC2) return 0;
  if (c == 0xE2 && (hammer_byte(text, i + 1) == 0x80 || hammer_byte(text, i + 1) == 0x81)) return 0;
  return c >= 0xC0;
}

// Bytes in the apostrophe at i, straight, U+2019, or Ukrainian's U+02BC, or 0.
static inline uint64_t hammer_apostrophe_at(HammerText text, uint64_t i) {
  if (hammer_byte(text, i) == '\'') return 1;
  if (hammer_byte(text, i) == 0xCA && hammer_byte(text, i + 1) == 0xBC) return 2;
  return hammer_char_is(text, i, 0x2019) ? 3 : 0;
}

// Whether the character before i, when there is one, is a letter.
static inline int hammer_letter_before(HammerText text, uint64_t i) {
  return i > 0 && hammer_letter_at(text, hammer_char_before(text, i));
}

// The words of a text: letters, and apostrophes between letters, so "don't" is one word. Digits and
// hyphens end a word. The list is in hammer_must_alloc's memory.
static inline HammerWord *hammer_words(HammerText text, uint64_t *count) {
  HammerWord *words = hammer_must_alloc((text.length / 2 + 1) * sizeof(HammerWord));
  uint64_t n = 0;
  uint64_t i = 0;
  while (i < text.length) {
    if (!hammer_letter_at(text, i)) {
      i += hammer_char_length(text, i);
      continue;
    }
    uint64_t start = i;
    while (i < text.length) {
      uint64_t apostrophe;
      if (hammer_letter_at(text, i)) {
        i += hammer_char_length(text, i);
      } else if ((apostrophe = hammer_apostrophe_at(text, i)) && hammer_letter_at(text, i + apostrophe)) {
        i += apostrophe;
      } else {
        break;
      }
    }
    words[n++] = (HammerWord){start, i};
  }
  *count = n;
  return words;
}

static inline uint64_t hammer_word_length(HammerWord word) { return word.end - word.start; }

// Whether the word is the first n bytes of `literal`, ignoring case unless `exact`. An apostrophe in the
// literal, straight or curly, matches any.
static inline int hammer_word_matches_case(HammerText text, HammerWord word, const char *literal, uint64_t n, int exact) {
  uint64_t at = word.start, i = 0;
  while (i < n) {
    if (at >= word.end) return 0;
    char have_byte = text.data[at], want_byte = literal[i];
    if ((unsigned char)have_byte < 0x80 && (unsigned char)want_byte < 0x80 && want_byte != '\'') {
      if (exact ? have_byte != want_byte : hammer_to_lower(have_byte) != hammer_to_lower(want_byte)) return 0;
      at++;
      i++;
      continue;
    }
    uint64_t literal_length, text_length;
    uint32_t want = hammer_decode(literal + i, n - i, &literal_length);
    if (want == '\'' || want == 0x2019) {
      uint64_t apostrophe = hammer_apostrophe_at(text, at);
      if (!apostrophe) return 0;
      at += apostrophe;
    } else {
      uint32_t have = hammer_decode(text.data + at, word.end - at, &text_length);
      if (exact ? have != want : hammer_lower(have) != hammer_lower(want)) return 0;
      at += text_length;
    }
    i += literal_length;
  }
  return at == word.end;
}

static inline int hammer_word_matches(HammerText text, HammerWord word, const char *literal, uint64_t n) {
  return hammer_word_matches_case(text, word, literal, n, 0);
}

// Whether the word is `literal`, as hammer_word_matches.
static inline int hammer_word_is(HammerText text, HammerWord word, const char *literal) {
  return hammer_word_matches(text, word, literal, strlen(literal));
}

// Whether the word is one of `literals`, a list ending with 0.
static inline int hammer_word_in(HammerText text, HammerWord word, const char *const *literals) {
  if (word.end == word.start) return 0;
  char first = hammer_to_lower(text.data[word.start]);
  for (; *literals; literals++) {
    // Most literals differ at their first letter, so they are passed over without a full match.
    char literal = (*literals)[0];
    if ((unsigned char)first < 0x80 && (unsigned char)literal < 0x80 && hammer_to_lower(literal) != first) continue;
    if (hammer_word_is(text, word, *literals)) return 1;
  }
  return 0;
}

static inline int hammer_word_starts_with(HammerText text, HammerWord word, const char *prefix) {
  uint64_t n = strlen(prefix);
  return hammer_word_length(word) >= n &&
         hammer_word_matches(text, (HammerWord){word.start, word.start + n}, prefix, n);
}

static inline int hammer_same_word(HammerText text, HammerWord a, HammerWord b) {
  uint64_t i = a.start, j = b.start;
  while (i < a.end && j < b.end) {
    uint64_t n, m;
    uint32_t x = hammer_decode(text.data + i, a.end - i, &n);
    uint32_t y = hammer_decode(text.data + j, b.end - j, &m);
    if (hammer_lower(x) != hammer_lower(y)) return 0;
    i += n;
    j += m;
  }
  return i == a.end && j == b.end;
}

// Whether every letter of the word is a capital.
static inline int hammer_all_upper(HammerText text, HammerWord word) {
  for (uint64_t i = word.start; i < word.end; i += hammer_char_length(text, i)) {
    if (hammer_letter_at(text, i) && !hammer_upper_at(text, i)) return 0;
  }
  return 1;
}

// Whether only spaces lie between two words, at least one, so they read as one phrase.
static inline int hammer_spaced(HammerText text, HammerWord a, HammerWord b) {
  if (b.start <= a.end) return 0;
  for (uint64_t i = a.end; i < b.start; i++) {
    if (text.data[i] != ' ') return 0;
  }
  return 1;
}

// How many words of `phrase` match from words[i] on, or 0: its first word in either case, as at the
// start of a sentence, and the rest as written, so "de el" can pass "de El Salvador". A word "*"
// matches any word. A space in the phrase matches spaces or one hyphen between words, and a hyphen
// matches only a hyphen.
static inline uint64_t hammer_match_phrase(HammerText text, const HammerWord *words, uint64_t count, uint64_t i,
                                           const char *phrase) {
  uint64_t matched = 0;
  const char *at = phrase;
  for (;;) {
    if (i + matched >= count) return 0;
    const char *end = at;
    while (*end && *end != ' ' && *end != '-') end++;
    HammerWord word = words[i + matched];
    uint64_t n = (uint64_t)(end - at);
    if (!(n == 1 && *at == '*') && !hammer_word_matches_case(text, word, at, n, matched > 0)) return 0;
    matched++;
    if (!*end) return matched;
    if (i + matched >= count) return 0;
    HammerWord next = words[i + matched];
    int hyphen = next.start == word.end + 1 && text.data[word.end] == '-';
    if (*end == '-' ? !hyphen : !hyphen && !hammer_spaced(text, word, next)) return 0;
    at = end + 1;
  }
}

#endif // HAMMER_TEXT_H
