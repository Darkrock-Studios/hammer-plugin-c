// A small JSON reader for Hammer plugins: walks a JSON document in place, without allocating.
//
//   HammerJson json;
//   hammer_json_init(&json, text, length);
//   const char *key; uint64_t key_length;
//   if (hammer_json_enter_object(&json)) {
//     while (hammer_json_next_key(&json, &key, &key_length)) {
//       if (hammer_json_key_is(key, key_length, "name")) { ...read the value... }
//       else hammer_json_skip(&json);
//     }
//   }
//
// Every value must be read or skipped before moving on. A malformed document sets `error` and every
// later call returns nothing, so check it once at the end.
#ifndef HAMMER_JSON_H
#define HAMMER_JSON_H

#include <stdint.h>

typedef struct {
  const char *text;
  uint64_t length;
  uint64_t at;
  int error;
} HammerJson;

static inline void hammer_json_init(HammerJson *json, const char *text, uint64_t length) {
  json->text = text;
  json->length = length;
  json->at = 0;
  json->error = 0;
}

static inline void hammer_json_space(HammerJson *json) {
  while (json->at < json->length) {
    char c = json->text[json->at];
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
    json->at++;
  }
}

// The first character of the next value: '{', '[', '"', 't', 'f', 'n', or a digit or '-'; 0 at the end or after an error.
static inline char hammer_json_peek(HammerJson *json) {
  hammer_json_space(json);
  return json->error || json->at >= json->length ? 0 : json->text[json->at];
}

static inline int hammer_json_expect(HammerJson *json, char c) {
  if (hammer_json_peek(json) != c) {
    json->error = 1;
    return 0;
  }
  json->at++;
  return 1;
}

// Reads a string, giving its contents as they appear, escapes included; see hammer_json_unescape.
static inline int hammer_json_string(HammerJson *json, const char **raw, uint64_t *raw_length) {
  if (!hammer_json_expect(json, '"')) return 0;
  uint64_t start = json->at;
  while (json->at < json->length && json->text[json->at] != '"') {
    json->at += json->text[json->at] == '\\' ? 2 : 1;
  }
  if (json->at >= json->length) {
    json->error = 1;
    return 0;
  }
  *raw = json->text + start;
  *raw_length = json->at - start;
  json->at++;
  return 1;
}

static inline int hammer_json_enter_object(HammerJson *json) { return hammer_json_expect(json, '{'); }
static inline int hammer_json_enter_array(HammerJson *json) { return hammer_json_expect(json, '['); }

// Moves to the object's next key, reading it; returns 0, past the closing brace, when there are no more.
static inline int hammer_json_next_key(HammerJson *json, const char **key, uint64_t *key_length) {
  char c = hammer_json_peek(json);
  if (c == '}') {
    json->at++;
    return 0;
  }
  if (c == ',') {
    json->at++;
    c = hammer_json_peek(json);
  }
  if (c != '"' || !hammer_json_string(json, key, key_length)) {
    json->error = 1;
    return 0;
  }
  return hammer_json_expect(json, ':');
}

// Moves to the array's next item; returns 0, past the closing bracket, when there are no more.
static inline int hammer_json_next_item(HammerJson *json) {
  char c = hammer_json_peek(json);
  if (c == ']') {
    json->at++;
    return 0;
  }
  if (c == ',') {
    json->at++;
    c = hammer_json_peek(json);
  }
  if (c == 0) {
    json->error = 1;
    return 0;
  }
  return 1;
}

// Whether a key, or a string's raw contents, is exactly `literal` (which must need no escaping).
static inline int hammer_json_key_is(const char *key, uint64_t key_length, const char *literal) {
  uint64_t i = 0;
  for (; i < key_length; i++) {
    if (literal[i] == 0 || literal[i] != key[i]) return 0;
  }
  return literal[i] == 0;
}

static inline void hammer_json_skip(HammerJson *json) {
  char c = hammer_json_peek(json);
  const char *raw;
  uint64_t raw_length;
  if (c == '"') {
    hammer_json_string(json, &raw, &raw_length);
  } else if (c == '{') {
    json->at++;
    while (hammer_json_next_key(json, &raw, &raw_length)) hammer_json_skip(json);
  } else if (c == '[') {
    json->at++;
    while (hammer_json_next_item(json)) hammer_json_skip(json);
  } else if (c == 0) {
    json->error = 1;
  } else {
    // A number, true, false, or null: everything up to the next delimiter.
    while (json->at < json->length) {
      char d = json->text[json->at];
      if (d == ',' || d == '}' || d == ']' || d == ' ' || d == '\n' || d == '\r' || d == '\t') break;
      json->at++;
    }
  }
}

static inline int hammer_json_bool(HammerJson *json) {
  int value = hammer_json_peek(json) == 't';
  hammer_json_skip(json);
  return value;
}

static inline int64_t hammer_json_int(HammerJson *json) {
  hammer_json_peek(json);
  int negative = json->at < json->length && json->text[json->at] == '-';
  if (negative) json->at++;
  int64_t value = 0;
  while (json->at < json->length && json->text[json->at] >= '0' && json->text[json->at] <= '9') {
    value = value * 10 + (json->text[json->at++] - '0');
  }
  hammer_json_skip(json); // any fraction or exponent
  return negative ? -value : value;
}

static inline uint32_t hammer_json_hex(const char *digits) {
  uint32_t value = 0;
  for (int i = 0; i < 4; i++) {
    char c = digits[i];
    value <<= 4;
    if (c >= '0' && c <= '9') value |= (uint32_t)(c - '0');
    else if (c >= 'a' && c <= 'f') value |= (uint32_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') value |= (uint32_t)(c - 'A' + 10);
  }
  return value;
}

// Decodes a string's raw contents into `out` as UTF-8, returning its length. Never longer than the
// raw contents, so `out` needs at most raw_length bytes.
static inline uint64_t hammer_json_unescape(const char *raw, uint64_t raw_length, char *out) {
  uint64_t length = 0;
  for (uint64_t i = 0; i < raw_length; i++) {
    char c = raw[i];
    if (c != '\\' || i + 1 >= raw_length) {
      out[length++] = c;
      continue;
    }
    char escaped = raw[++i];
    switch (escaped) {
      case 'n': out[length++] = '\n'; break;
      case 't': out[length++] = '\t'; break;
      case 'r': out[length++] = '\r'; break;
      case 'b': out[length++] = '\b'; break;
      case 'f': out[length++] = '\f'; break;
      case 'u': {
        if (i + 4 >= raw_length) return length;
        uint32_t code = hammer_json_hex(raw + i + 1);
        i += 4;
        // A high surrogate followed by \uDC00-\uDFFF is one character outside the basic plane.
        if (code >= 0xD800 && code <= 0xDBFF && i + 6 < raw_length && raw[i + 1] == '\\' && raw[i + 2] == 'u') {
          uint32_t low = hammer_json_hex(raw + i + 3);
          if (low >= 0xDC00 && low <= 0xDFFF) {
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            i += 6;
          }
        }
        if (code < 0x80) {
          out[length++] = (char)code;
        } else if (code < 0x800) {
          out[length++] = (char)(0xC0 | (code >> 6));
          out[length++] = (char)(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
          out[length++] = (char)(0xE0 | (code >> 12));
          out[length++] = (char)(0x80 | ((code >> 6) & 0x3F));
          out[length++] = (char)(0x80 | (code & 0x3F));
        } else {
          out[length++] = (char)(0xF0 | (code >> 18));
          out[length++] = (char)(0x80 | ((code >> 12) & 0x3F));
          out[length++] = (char)(0x80 | ((code >> 6) & 0x3F));
          out[length++] = (char)(0x80 | (code & 0x3F));
        }
        break;
      }
      default: out[length++] = escaped; break; // \" \\ \/
    }
  }
  return length;
}

// Finds key in the JSON object text, leaving json at its value for the readers above; 0 when the text
// is not an object or has no such key.
static inline int hammer_json_find(const char *object, uint64_t length, const char *key, HammerJson *json) {
  if (!object) return 0;
  hammer_json_init(json, object, length);
  const char *found;
  uint64_t found_length;
  if (!hammer_json_enter_object(json)) return 0;
  while (hammer_json_next_key(json, &found, &found_length)) {
    if (hammer_json_key_is(found, found_length, key)) return 1;
    hammer_json_skip(json);
  }
  return 0;
}

#endif // HAMMER_JSON_H
