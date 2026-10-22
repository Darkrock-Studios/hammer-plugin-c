// A `diagnose` export for checks the manifest declares under [[diagnostics]]: the kit reads the
// request and writes the reply, and the plugin defines two functions.
//
//   int hammer_diagnose_begin(const HammerDiagnoseRequest *request);
//     Called once per request; returns 0 to report nothing, say for a language it doesn't check.
//   void hammer_diagnose_paragraph(HammerText paragraph);
//     Called for each paragraph, as plain UTF-8, to report what it finds with hammer_report.
//
// Include it after hammer.h. In the file that defines HAMMER_IMPLEMENTATION, it defines the export.
#ifndef HAMMER_DIAGNOSE_H
#define HAMMER_DIAGNOSE_H

#include "hammer.h"
#include "hammer_json.h"
#include "hammer_out.h"
#include "hammer_text.h"

typedef struct {
  // The [[diagnostics]] entry's name.
  HammerText name;
  // The project's language as sent, such as "en" or "en-US"; data is 0 when Hammer has none.
  HammerText language;
  // The settings object's JSON; data is 0 when there is none.
  HammerText settings;
  // The UI's BCP 47 tag, for writing messages in the user's language; data is 0 when unknown. Not the
  // project's language, which is what the text is written in.
  HammerText locale;
} HammerDiagnoseRequest;

// A fix: replacement text for the diagnostic's range, shown as itself, or as label when given.
typedef struct {
  const char *replacement;
  uint64_t length;
  const char *label;
} HammerFix;

int hammer_diagnose_begin(const HammerDiagnoseRequest *request);
void hammer_diagnose_paragraph(HammerText paragraph);

// Reports bytes [start, end) of the current paragraph with a message and any fixes.
void hammer_report(uint64_t start, uint64_t end, const char *message, const HammerFix *fixes, uint32_t fix_count);

// hammer_report with one plain fix, or none when replacement is 0.
static inline void hammer_report_fix(uint64_t start, uint64_t end, const char *message, const char *replacement,
                                     uint64_t length) {
  HammerFix fix = {replacement, length, 0};
  hammer_report(start, end, message, replacement ? &fix : 0, replacement ? 1 : 0);
}

// Reads a request and returns the reply. Call hammer_alloc_reset() first, keeping input out of
// hammer_alloc's memory or allocated before the reset.
const char *hammer_diagnose_run(const char *input, uint64_t size, uint64_t *reply_length);

// Finds a setting's value in the request, for hammer_json to read; 0 when it's not there.
static inline int hammer_setting(const HammerDiagnoseRequest *request, const char *key, HammerJson *json) {
  return hammer_json_find(request->settings.data, request->settings.length, key, json);
}

static inline int hammer_setting_bool(const HammerDiagnoseRequest *request, const char *key, int fallback) {
  HammerJson json;
  if (!hammer_setting(request, key, &json)) return fallback;
  char c = hammer_json_peek(&json);
  return c == 't' || c == 'f' ? hammer_json_bool(&json) : fallback;
}

static inline int64_t hammer_setting_int(const HammerDiagnoseRequest *request, const char *key, int64_t fallback) {
  HammerJson json;
  if (!hammer_setting(request, key, &json)) return fallback;
  char c = hammer_json_peek(&json);
  return c == '-' || hammer_is_digit(c) ? hammer_json_int(&json) : fallback;
}

// Whether the request's language is `code` ("en"), with or without a region ("en-US", "en_GB").
static inline int hammer_language_is(const HammerDiagnoseRequest *request, const char *code) {
  HammerText language = request->language;
  uint64_t n = strlen(code);
  if (!language.data || language.length < n) return 0;
  for (uint64_t i = 0; i < n; i++) {
    if (hammer_to_lower(language.data[i]) != code[i]) return 0;
  }
  return language.length == n || language.data[n] == '-' || language.data[n] == '_';
}

#ifdef HAMMER_IMPLEMENTATION
static HammerBuffer hammer_diagnose_reply;
static uint64_t hammer_diagnose_index;
static int hammer_diagnose_reported;

void hammer_report(uint64_t start, uint64_t end, const char *message, const HammerFix *fixes, uint32_t fix_count) {
  HammerBuffer *out = &hammer_diagnose_reply;
  hammer_put_text(out, hammer_diagnose_reported ? ",{\"paragraph\":" : "{\"paragraph\":");
  hammer_diagnose_reported = 1;
  hammer_put_int(out, (int64_t)hammer_diagnose_index);
  hammer_put_text(out, ",\"start\":");
  hammer_put_int(out, (int64_t)start);
  hammer_put_text(out, ",\"end\":");
  hammer_put_int(out, (int64_t)end);
  hammer_put_text(out, ",\"message\":");
  hammer_put_json_string(out, message, strlen(message));
  hammer_put_text(out, ",\"fixes\":[");
  for (uint32_t i = 0; i < fix_count; i++) {
    if (i) hammer_put_char(out, ',');
    if (fixes[i].label) {
      hammer_put_text(out, "{\"replacement\":");
      hammer_put_json_string(out, fixes[i].replacement, fixes[i].length);
      hammer_put_text(out, ",\"label\":");
      hammer_put_json_string(out, fixes[i].label, strlen(fixes[i].label));
      hammer_put_char(out, '}');
    } else {
      hammer_put_json_string(out, fixes[i].replacement, fixes[i].length);
    }
  }
  hammer_put_text(out, "]}");
}

const char *hammer_diagnose_run(const char *input, uint64_t size, uint64_t *reply_length) {
  hammer_diagnose_reply = (HammerBuffer){0};
  hammer_diagnose_index = 0;
  hammer_diagnose_reported = 0;

  // Found before the paragraphs are checked, since keys may come in any order.
  HammerDiagnoseRequest request = {{0}, {0}, {0}, {0}};
  HammerJson json;
  hammer_json_init(&json, input, size);
  const char *key;
  uint64_t key_length, paragraphs_at = 0;
  int has_paragraphs = 0;
  if (hammer_json_enter_object(&json)) {
    while (hammer_json_next_key(&json, &key, &key_length)) {
      if (hammer_json_key_is(key, key_length, "diagnostics") && hammer_json_peek(&json) == '"') {
        hammer_json_string(&json, &request.name.data, &request.name.length);
      } else if (hammer_json_key_is(key, key_length, "language") && hammer_json_peek(&json) == '"') {
        hammer_json_string(&json, &request.language.data, &request.language.length);
      } else if (hammer_json_key_is(key, key_length, "locale") && hammer_json_peek(&json) == '"') {
        hammer_json_string(&json, &request.locale.data, &request.locale.length);
      } else if (hammer_json_key_is(key, key_length, "settings") && hammer_json_peek(&json) == '{') {
        uint64_t start = json.at;
        hammer_json_skip(&json);
        request.settings = (HammerText){input + start, json.at - start};
      } else if (hammer_json_key_is(key, key_length, "paragraphs")) {
        has_paragraphs = 1;
        paragraphs_at = json.at;
        hammer_json_skip(&json);
      } else {
        hammer_json_skip(&json);
      }
    }
  }
  if (json.error) hammer_fail("Hammer sent a request the plugin could not read");

  hammer_put_text(&hammer_diagnose_reply, "{\"diagnostics\":[");
  if (has_paragraphs && hammer_diagnose_begin(&request)) {
    json.at = paragraphs_at;
    if (hammer_json_enter_array(&json)) {
      while (hammer_json_next_item(&json)) {
        const char *raw;
        uint64_t raw_length;
        if (hammer_json_string(&json, &raw, &raw_length)) {
          char *decoded = hammer_must_alloc(raw_length + 1);
          uint64_t length = hammer_json_unescape(raw, raw_length, decoded);
          hammer_diagnose_paragraph((HammerText){decoded, length});
        } else {
          hammer_json_skip(&json);
        }
        hammer_diagnose_index++;
      }
    }
  }
  hammer_put_text(&hammer_diagnose_reply, "]}");
  *reply_length = hammer_diagnose_reply.length;
  return hammer_diagnose_reply.data;
}

#ifndef HAMMER_NATIVE
int32_t EXTISM_EXPORTED_FUNCTION(diagnose) {
  hammer_alloc_reset();
  uint64_t size = extism_input_length();
  char *input = hammer_must_alloc(size);
  extism_load_input(0, input, size);
  uint64_t length;
  const char *reply = hammer_diagnose_run(input, size, &length);
  extism_output_buf(reply, length);
  return 0;
}
#endif // HAMMER_NATIVE
#endif // HAMMER_IMPLEMENTATION

#endif // HAMMER_DIAGNOSE_H
