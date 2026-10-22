// Native unit tests for Hammer plugins. A plugin's test.c includes its source, then this header, and
// c/test.sh builds it with HAMMER_NATIVE and runs it:
//
//   #include "simple-grammar.c"
//   #include "hammer_test.h"
//
//   int main(void) {
//     HAMMER_EXPECT(0, "I saw the the cat.", "[the the]>the");
//     return hammer_test_summary();
//   }
//
// HAMMER_EXPECT checks a diagnose plugin's findings in one English paragraph, given the settings
// object's JSON or 0; HAMMER_EXPECT_IN in another language's. Each finding reads "[text]>fix|fix", text being what it covers; a labelled fix
// reads "replacement{label}". Findings are joined by "; ", and "" means none.
#ifndef HAMMER_TEST_H
#define HAMMER_TEST_H

#ifndef HAMMER_NATIVE
#error "hammer_test.h is for native builds; see c/test.sh"
#endif

// The [[diagnostics]] name HAMMER_EXPECT asks for; define it before including this header to change it.
#ifndef HAMMER_TEST_DIAGNOSTICS
#define HAMMER_TEST_DIAGNOSTICS "grammar"
#endif

static int hammer_test_run_count;
static int hammer_test_failures;

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} HammerTestString;

static void hammer_test_append(HammerTestString *s, const char *text, size_t length) {
  if (s->length + length + 1 > s->capacity) {
    s->capacity = (s->length + length + 1) * 2;
    s->data = realloc(s->data, s->capacity);
  }
  memcpy(s->data + s->length, text, length);
  s->length += length;
  s->data[s->length] = 0;
}

static void hammer_test_append_sz(HammerTestString *s, const char *text) { hammer_test_append(s, text, strlen(text)); }

static void hammer_test_check(int passed, const char *what, const char *file, int line) {
  hammer_test_run_count++;
  if (passed) return;
  hammer_test_failures++;
  fprintf(stderr, "%s:%d: failed: %s\n", file, line, what);
}

#define HAMMER_CHECK(condition) hammer_test_check((condition), #condition, __FILE__, __LINE__)

#ifdef HAMMER_DIAGNOSE_H
// Runs a whole diagnose request, returning the reply; free it.
static char *hammer_test_run(const char *request) {
  hammer_alloc_reset();
  uint64_t length;
  const char *reply = hammer_diagnose_run(request, strlen(request), &length);
  char *copy = malloc(length + 1);
  memcpy(copy, reply, length);
  copy[length] = 0;
  return copy;
}

static void hammer_test_append_json_string(HammerTestString *s, const char *text) {
  hammer_test_append_sz(s, "\"");
  for (; *text; text++) {
    unsigned char c = (unsigned char)*text;
    if (c < 0x20) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", c);
      hammer_test_append_sz(s, escaped);
      continue;
    }
    if (c == '"' || c == '\\') hammer_test_append_sz(s, "\\");
    hammer_test_append(s, text, 1);
  }
  hammer_test_append_sz(s, "\"");
}

static void hammer_test_append_decoded(HammerTestString *s, HammerJson *json) {
  const char *raw;
  uint64_t raw_length;
  if (!hammer_json_string(json, &raw, &raw_length)) return;
  char *decoded = malloc(raw_length + 1);
  hammer_test_append(s, decoded, hammer_json_unescape(raw, raw_length, decoded));
  free(decoded);
}

// Renders a reply about one paragraph as HAMMER_EXPECT describes, or with each finding's message in
// place of its fixes.
static char *hammer_test_render(const char *reply, const char *paragraph, int messages) {
  HammerTestString out = {0};
  hammer_test_append(&out, "", 0);
  HammerJson json;
  hammer_json_init(&json, reply, strlen(reply));
  const char *key;
  uint64_t key_length;
  hammer_json_enter_object(&json);
  while (hammer_json_next_key(&json, &key, &key_length)) {
    if (!hammer_json_key_is(key, key_length, "diagnostics")) {
      hammer_json_skip(&json);
      continue;
    }
    hammer_json_enter_array(&json);
    while (hammer_json_next_item(&json)) {
      int64_t start = 0, end = 0;
      HammerTestString rest = {0};
      hammer_test_append(&rest, "", 0);
      hammer_json_enter_object(&json);
      while (hammer_json_next_key(&json, &key, &key_length)) {
        if (hammer_json_key_is(key, key_length, "start")) {
          start = hammer_json_int(&json);
        } else if (hammer_json_key_is(key, key_length, "end")) {
          end = hammer_json_int(&json);
        } else if (messages && hammer_json_key_is(key, key_length, "message")) {
          hammer_test_append_sz(&rest, " ");
          hammer_test_append_decoded(&rest, &json);
        } else if (!messages && hammer_json_key_is(key, key_length, "fixes")) {
          int first = 1;
          hammer_json_enter_array(&json);
          while (hammer_json_next_item(&json)) {
            hammer_test_append_sz(&rest, first ? ">" : "|");
            first = 0;
            if (hammer_json_peek(&json) == '"') {
              hammer_test_append_decoded(&rest, &json);
              continue;
            }
            HammerTestString label = {0};
            hammer_test_append(&label, "", 0);
            hammer_json_enter_object(&json);
            while (hammer_json_next_key(&json, &key, &key_length)) {
              if (hammer_json_key_is(key, key_length, "replacement")) hammer_test_append_decoded(&rest, &json);
              else if (hammer_json_key_is(key, key_length, "label")) hammer_test_append_decoded(&label, &json);
              else hammer_json_skip(&json);
            }
            hammer_test_append_sz(&rest, "{");
            hammer_test_append_sz(&rest, label.data);
            hammer_test_append_sz(&rest, "}");
            free(label.data);
          }
        } else {
          hammer_json_skip(&json);
        }
      }
      if (out.length) hammer_test_append_sz(&out, "; ");
      hammer_test_append_sz(&out, "[");
      if (start >= 0 && end >= start && (size_t)end <= strlen(paragraph)) {
        hammer_test_append(&out, paragraph + start, (size_t)(end - start));
      } else {
        hammer_test_append_sz(&out, "<out of range>");
      }
      hammer_test_append_sz(&out, "]");
      hammer_test_append_sz(&out, rest.data);
      free(rest.data);
    }
  }
  if (json.error) hammer_test_append_sz(&out, "<malformed reply>");
  return out.data;
}

static char *hammer_test_paragraph(const char *language, const char *settings, const char *paragraph, int messages) {
  HammerTestString request = {0};
  hammer_test_append_sz(&request, "{\"diagnostics\":\"" HAMMER_TEST_DIAGNOSTICS "\",\"language\":\"");
  hammer_test_append_sz(&request, language);
  hammer_test_append_sz(&request, "\",\"settings\":");
  hammer_test_append_sz(&request, settings ? settings : "{}");
  hammer_test_append_sz(&request, ",\"paragraphs\":[");
  hammer_test_append_json_string(&request, paragraph);
  hammer_test_append_sz(&request, "]}");
  char *reply = hammer_test_run(request.data);
  char *rendered = hammer_test_render(reply, paragraph, messages);
  free(request.data);
  free(reply);
  return rendered;
}

static void hammer_test_expect(const char *language, const char *settings, const char *paragraph, const char *expected,
                               int messages, const char *file, int line) {
  char *actual = hammer_test_paragraph(language, settings, paragraph, messages);
  hammer_test_run_count++;
  if (strcmp(actual, expected) != 0) {
    hammer_test_failures++;
    fprintf(stderr, "%s:%d: %s\n  expected: %s\n  actual:   %s\n", file, line, paragraph, expected, actual);
  }
  free(actual);
}

#define HAMMER_EXPECT(settings, paragraph, expected) \
  hammer_test_expect("en", (settings), (paragraph), (expected), 0, __FILE__, __LINE__)
#define HAMMER_EXPECT_IN(language, settings, paragraph, expected) \
  hammer_test_expect((language), (settings), (paragraph), (expected), 0, __FILE__, __LINE__)
// As HAMMER_EXPECT, with each finding's message after its text: "[the the] Repeated word".
#define HAMMER_EXPECT_MESSAGES(settings, paragraph, expected) \
  hammer_test_expect("en", (settings), (paragraph), (expected), 1, __FILE__, __LINE__)
#define HAMMER_EXPECT_MESSAGES_IN(language, settings, paragraph, expected) \
  hammer_test_expect((language), (settings), (paragraph), (expected), 1, __FILE__, __LINE__)
#endif // HAMMER_DIAGNOSE_H

// Prints a summary; returns main's exit status.
static int hammer_test_summary(void) {
  if (hammer_test_failures) {
    fprintf(stderr, "%d of %d checks failed\n", hammer_test_failures, hammer_test_run_count);
    return 1;
  }
  printf("%d checks passed\n", hammer_test_run_count);
  return 0;
}

#endif // HAMMER_TEST_H
