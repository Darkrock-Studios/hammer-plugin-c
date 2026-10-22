// The `action` export's request and replies.
//
//   HammerActionRequest request;
//   if (!hammer_action_read(input, size, &request)) hammer_fail("Hammer sent a request this could not read");
//   int64_t count = hammer_input_int(&request, "count", 8);
//
// Strings in the request are JSON string contents as sent, escapes and all, so they go back into JSON as
// they are; hammer_json_unescape decodes one for display. Every reply is written with one of the
// hammer_put_ writers below: a message, a document, a page of fields and buttons, errors, or an item to
// open.
//
//   HammerField fields[] = {{.type = HAMMER_FIELD_TEXT, .key = "name", .label = "Name", .required = 1}};
//   HammerButton buttons[] = {{.id = "back", .label = "Back", .no_validate = 1},
//                             {.id = "next", .label = "Next", .primary = 1}};
//   HammerPage page = {.title = "Who?", .fields = fields, .field_count = 1,
//                      .buttons = buttons, .button_count = 2, .state = "{\"step\":2}"};
//   hammer_put_page(&out, &page, 0);
#ifndef HAMMER_ACTION_H
#define HAMMER_ACTION_H

#include "hammer.h"
#include "hammer_json.h"
#include "hammer_out.h"
#include "hammer_text.h"

typedef struct {
  // The [[actions]] entry's name.
  HammerText action;
  HammerText project;
  // The screen it was run from: project, scene, note, entry, or event.
  HammerText place;
  // The id of the item that screen shows, when has_id is set.
  int64_t id;
  int has_id;
  // The id of the button pressed; data is 0 on the first call, which opens the action or runs the
  // manifest's [[actions.field]] page.
  HammerText button;
  // Reserved for fields that call the action when they change; data is 0.
  HammerText changed;
  // The UI's BCP 47 tag, for writing replies in the user's language; data is 0 when unknown.
  HammerText locale;
  // The input and settings objects' JSON: a value for each field of the page the user was on, and for
  // each declared setting.
  HammerText input;
  HammerText settings;
  // The last page's state: the JSON value it was written with. data is 0 on the first call, and after a
  // page without one.
  HammerText state;
  // Set on the first call after Hammer restored a run the user had left.
  int resumed;
} HammerActionRequest;

// Reads the request in text; 0 when it is malformed or names no action or project.
static inline int hammer_action_read(const char *text, uint64_t length, HammerActionRequest *request) {
  *request = (HammerActionRequest){0};
  HammerJson json;
  hammer_json_init(&json, text, length);
  const char *key;
  uint64_t key_length;
  if (!hammer_json_enter_object(&json)) return 0;
  while (hammer_json_next_key(&json, &key, &key_length)) {
    char next = hammer_json_peek(&json);
    if (next == '"' && hammer_json_key_is(key, key_length, "action")) {
      hammer_json_string(&json, &request->action.data, &request->action.length);
    } else if (next == '"' && hammer_json_key_is(key, key_length, "project")) {
      hammer_json_string(&json, &request->project.data, &request->project.length);
    } else if (next == '"' && hammer_json_key_is(key, key_length, "button")) {
      hammer_json_string(&json, &request->button.data, &request->button.length);
    } else if (next == '"' && hammer_json_key_is(key, key_length, "changed")) {
      hammer_json_string(&json, &request->changed.data, &request->changed.length);
    } else if (next == '"' && hammer_json_key_is(key, key_length, "locale")) {
      hammer_json_string(&json, &request->locale.data, &request->locale.length);
    } else if ((next == 't' || next == 'f') && hammer_json_key_is(key, key_length, "resumed")) {
      request->resumed = hammer_json_bool(&json);
    } else if (next && next != 'n' && hammer_json_key_is(key, key_length, "state")) {
      uint64_t start = json.at;
      hammer_json_skip(&json);
      request->state = (HammerText){text + start, json.at - start};
    } else if (next == '{' && (hammer_json_key_is(key, key_length, "input") || hammer_json_key_is(key, key_length, "settings"))) {
      uint64_t start = json.at;
      hammer_json_skip(&json);
      HammerText object = {text + start, json.at - start};
      if (hammer_json_key_is(key, key_length, "input")) request->input = object;
      else request->settings = object;
    } else if (next == '{' && hammer_json_key_is(key, key_length, "context")) {
      hammer_json_enter_object(&json);
      while (hammer_json_next_key(&json, &key, &key_length)) {
        char value = hammer_json_peek(&json);
        if (value == '"' && hammer_json_key_is(key, key_length, "place")) {
          hammer_json_string(&json, &request->place.data, &request->place.length);
        } else if ((value == '-' || hammer_is_digit(value)) && hammer_json_key_is(key, key_length, "id")) {
          request->id = hammer_json_int(&json);
          request->has_id = 1;
        } else {
          hammer_json_skip(&json);
        }
      }
    } else {
      hammer_json_skip(&json);
    }
  }
  return !json.error && request->action.data && request->project.data;
}

// Whether a request's string, such as its action or place, is literal.
static inline int hammer_text_is(HammerText text, const char *literal) {
  return text.data && hammer_json_key_is(text.data, text.length, literal);
}

// Finds a field's value in the request's input, for hammer_json to read; 0 when it's not there.
static inline int hammer_input(const HammerActionRequest *request, const char *key, HammerJson *json) {
  return hammer_json_find(request->input.data, request->input.length, key, json);
}

static inline int hammer_input_bool(const HammerActionRequest *request, const char *key, int fallback) {
  HammerJson json;
  if (!hammer_input(request, key, &json)) return fallback;
  char c = hammer_json_peek(&json);
  return c == 't' || c == 'f' ? hammer_json_bool(&json) : fallback;
}

static inline int64_t hammer_input_int(const HammerActionRequest *request, const char *key, int64_t fallback) {
  HammerJson json;
  if (!hammer_input(request, key, &json)) return fallback;
  char c = hammer_json_peek(&json);
  return c == '-' || hammer_is_digit(c) ? hammer_json_int(&json) : fallback;
}

// A text or choice field's value as sent; data is 0 when it has none.
static inline HammerText hammer_input_string(const HammerActionRequest *request, const char *key) {
  HammerText value = {0, 0};
  HammerJson json;
  if (hammer_input(request, key, &json) && hammer_json_peek(&json) == '"') hammer_json_string(&json, &value.data, &value.length);
  return value;
}

// Reads the value at json, moving past it, as an entity field's pick, {"kind":"entry","id":12}: its kind
// (scene, note, entry, or event) and id. 0 for null, or anything else that is not a pick. For a field
// with multiple, step through its list with hammer_json_enter_array and hammer_json_next_item.
static inline int hammer_entity_read(HammerJson *json, HammerText *kind, int64_t *id) {
  *kind = (HammerText){0, 0};
  *id = 0;
  if (hammer_json_peek(json) != '{') {
    hammer_json_skip(json);
    return 0;
  }
  int has_id = 0;
  const char *key;
  uint64_t key_length;
  hammer_json_enter_object(json);
  while (hammer_json_next_key(json, &key, &key_length)) {
    char c = hammer_json_peek(json);
    if (c == '"' && hammer_json_key_is(key, key_length, "kind")) {
      hammer_json_string(json, &kind->data, &kind->length);
    } else if ((c == '-' || hammer_is_digit(c)) && hammer_json_key_is(key, key_length, "id")) {
      *id = hammer_json_int(json);
      has_id = 1;
    } else {
      hammer_json_skip(json);
    }
  }
  if (!json->error && kind->data && has_id) return 1;
  *kind = (HammerText){0, 0};
  *id = 0;
  return 0;
}

// A single entity field's pick: its kind and id, and 1; 0 when the field is null or not there.
static inline int hammer_input_entity(const HammerActionRequest *request, const char *key, HammerText *kind, int64_t *id) {
  HammerJson json;
  if (hammer_input(request, key, &json)) return hammer_entity_read(&json, kind, id);
  *kind = (HammerText){0, 0};
  *id = 0;
  return 0;
}

typedef enum {
  HAMMER_FIELD_TEXT,
  HAMMER_FIELD_BOOL,
  HAMMER_FIELD_INT,
  HAMMER_FIELD_CHOICE,
  HAMMER_FIELD_SCENES,
  HAMMER_FIELD_ENTITY
} HammerFieldType;

// What edits a text field: an ordinary text field, or the scene editor's, whose value is markdown,
// with its format bar or the extended one (highlight, task lists, tables).
typedef enum { HAMMER_TEXT_PLAIN, HAMMER_TEXT_RICH, HAMMER_TEXT_RICH_EXTENDED } HammerTextFormat;

typedef struct {
  const char *value;
  const char *label;
} HammerOption;

// A field of a page. Its value comes back in the next request's input under key: a string for text and
// choice, a bool, an integer, for scenes a list of scene ids (one id or null when single), and for entity
// one pick, {"kind":"entry","id":12}, or null (a list of picks when multiple). Members left 0 are left
// out, so a designated initializer names only what it needs.
//
//   static const char *const entries[] = {"entry"};
//   static const char *const people[] = {"person"};
//   HammerField who = {.type = HAMMER_FIELD_ENTITY, .key = "who", .label = "Who", .required = 1,
//                      .kinds = entries, .kind_count = 1, .entry_types = people, .entry_type_count = 1};
typedef struct {
  HammerFieldType type;
  const char *key;
  const char *label;
  const char *hint;
  // Text must not be blank, scenes empty, or an entity unpicked; Hammer checks before calling.
  int required;
  // The initial value as JSON, such as "true", "8", "\"elvish\"", "[3,4]", or
  // "{\"kind\":\"entry\",\"id\":12}"; 0 for the type's own: false, min or 0, the first option, "", or
  // none.
  const char *default_json;
  // An int's bounds, each used when has_min or has_max is set.
  int64_t min;
  int64_t max;
  int has_min;
  int has_max;
  // A choice's options, at least one.
  const HammerOption *options;
  uint32_t option_count;
  HammerTextFormat format;
  // A plain text field of several lines.
  int multiline;
  // Scenes as one scene instead of a list; only scenes read it.
  int single;
  // An entity's kinds of item: scene, note, entry, or event; all four when kind_count is 0.
  const char *const *kinds;
  uint32_t kind_count;
  // An entity's types of entry, with entry among its kinds: person, place, thing, event, or idea; any
  // when entry_type_count is 0.
  const char *const *entry_types;
  uint32_t entry_type_count;
  // Tags an entity's item needs one of; any item when tag_count is 0.
  const char *const *tags;
  uint32_t tag_count;
  // An entity as a list of picks instead of one; only an entity reads it, since scenes are a list unless
  // single.
  int multiple;
} HammerField;

// A button of a page: pressing it calls the action again with id as "button", the page's values as
// "input", and its state as "state". One marked is_inline sits under the markdown, for acting on
// something in it; the rest sit in the dialog's footer. no_validate skips Hammer's checks of the
// fields, for one such as Back.
typedef struct {
  const char *id;
  const char *label;
  int primary;
  int is_inline;
  int no_validate;
} HammerButton;

// What the dialog shows; every member may be 0. markdown_length and state_length are the lengths of
// markdown and state, or 0 for up to their NUL. state is any JSON value, handed back as the next
// request's state; Hammer never reads it, and fails a call whose state is over 1 MiB.
typedef struct {
  const char *title;
  const char *markdown;
  uint64_t markdown_length;
  const HammerField *fields;
  uint32_t field_count;
  const HammerButton *buttons;
  uint32_t button_count;
  const char *state;
  uint64_t state_length;
} HammerPage;

// A message for a field, by its key, or for the whole page with "" or 0.
typedef struct {
  const char *field;
  const char *message;
} HammerError;

static inline void hammer_put_json_text(HammerBuffer *out, const char *text) {
  hammer_put_json_string(out, text, strlen(text));
}

static inline void hammer_put_json_texts(HammerBuffer *out, const char *key, const char *const *texts, uint32_t count) {
  if (!count) return;
  hammer_put_text(out, key);
  for (uint32_t i = 0; i < count; i++) {
    hammer_put_char(out, i ? ',' : '[');
    hammer_put_json_text(out, texts[i]);
  }
  hammer_put_char(out, ']');
}

static inline void hammer_put_field(HammerBuffer *out, const HammerField *field) {
  static const char *const types[] = {"text", "bool", "int", "choice", "scenes", "entity"};
  static const char *const formats[] = {"plain", "rich", "rich-extended"};
  hammer_put_text(out, "{\"key\":");
  hammer_put_json_text(out, field->key);
  hammer_put_text(out, ",\"type\":\"");
  hammer_put_text(out, types[field->type]);
  hammer_put_text(out, "\",\"label\":");
  hammer_put_json_text(out, field->label);
  if (field->hint) {
    hammer_put_text(out, ",\"hint\":");
    hammer_put_json_text(out, field->hint);
  }
  if (field->required) hammer_put_text(out, ",\"required\":true");
  if (field->default_json) {
    hammer_put_text(out, ",\"default\":");
    hammer_put_text(out, field->default_json);
  }
  if (field->has_min) {
    hammer_put_text(out, ",\"min\":");
    hammer_put_int(out, field->min);
  }
  if (field->has_max) {
    hammer_put_text(out, ",\"max\":");
    hammer_put_int(out, field->max);
  }
  if (field->option_count) {
    hammer_put_text(out, ",\"options\":[");
    for (uint32_t i = 0; i < field->option_count; i++) {
      hammer_put_text(out, i ? ",{\"value\":" : "{\"value\":");
      hammer_put_json_text(out, field->options[i].value);
      hammer_put_text(out, ",\"label\":");
      hammer_put_json_text(out, field->options[i].label);
      hammer_put_char(out, '}');
    }
    hammer_put_char(out, ']');
  }
  if (field->format) {
    hammer_put_text(out, ",\"format\":\"");
    hammer_put_text(out, formats[field->format]);
    hammer_put_char(out, '"');
  }
  if (field->multiline) hammer_put_text(out, ",\"multiline\":true");
  hammer_put_json_texts(out, ",\"kinds\":", field->kinds, field->kind_count);
  hammer_put_json_texts(out, ",\"entry_types\":", field->entry_types, field->entry_type_count);
  hammer_put_json_texts(out, ",\"tags\":", field->tags, field->tag_count);
  if (field->type == HAMMER_FIELD_SCENES && field->single) hammer_put_text(out, ",\"multiple\":false");
  if (field->type == HAMMER_FIELD_ENTITY && field->multiple) hammer_put_text(out, ",\"multiple\":true");
  hammer_put_char(out, '}');
}

static inline void hammer_put_button(HammerBuffer *out, const HammerButton *button) {
  hammer_put_text(out, "{\"id\":");
  hammer_put_json_text(out, button->id);
  hammer_put_text(out, ",\"label\":");
  hammer_put_json_text(out, button->label);
  if (button->primary) hammer_put_text(out, ",\"primary\":true");
  if (button->is_inline) hammer_put_text(out, ",\"inline\":true");
  if (button->no_validate) hammer_put_text(out, ",\"validate\":false");
  hammer_put_char(out, '}');
}

// Writes the end of a reply: its message, if any, and the closing brace. first is set when nothing
// came before it in the object.
static inline void hammer_put_reply_end(HammerBuffer *out, const char *message, int first) {
  if (message) {
    hammer_put_text(out, first ? "\"message\":" : ",\"message\":");
    hammer_put_json_text(out, message);
  }
  hammer_put_char(out, '}');
}

// Writes a reply showing page, with a message to show briefly (0 for none). Without a page, the action
// ends.
static inline void hammer_put_page(HammerBuffer *out, const HammerPage *page, const char *message) {
  hammer_put_char(out, '{');
  if (page) {
    const char *separator = "";
    hammer_put_text(out, "\"page\":{");
    if (page->title) {
      hammer_put_text(out, "\"title\":");
      hammer_put_json_text(out, page->title);
      separator = ",";
    }
    if (page->markdown) {
      hammer_put_text(out, separator);
      hammer_put_text(out, "\"markdown\":");
      hammer_put_json_string(out, page->markdown, page->markdown_length ? page->markdown_length : strlen(page->markdown));
      separator = ",";
    }
    if (page->field_count) {
      hammer_put_text(out, separator);
      hammer_put_text(out, "\"fields\":[");
      for (uint32_t i = 0; i < page->field_count; i++) {
        if (i) hammer_put_char(out, ',');
        hammer_put_field(out, &page->fields[i]);
      }
      hammer_put_char(out, ']');
      separator = ",";
    }
    if (page->button_count) {
      hammer_put_text(out, separator);
      hammer_put_text(out, "\"buttons\":[");
      for (uint32_t i = 0; i < page->button_count; i++) {
        if (i) hammer_put_char(out, ',');
        hammer_put_button(out, &page->buttons[i]);
      }
      hammer_put_char(out, ']');
      separator = ",";
    }
    if (page->state) {
      hammer_put_text(out, separator);
      hammer_put_text(out, "\"state\":");
      hammer_put(out, page->state, page->state_length ? page->state_length : strlen(page->state));
    }
    hammer_put_char(out, '}');
  }
  hammer_put_reply_end(out, message, !page);
}

// Writes a reply that ends the action, showing message briefly.
static inline void hammer_put_message(HammerBuffer *out, const char *message) { hammer_put_page(out, 0, message); }

// Writes a reply showing length bytes of markdown, a report the user can copy or save as a note.
static inline void hammer_put_document(HammerBuffer *out, const char *markdown, uint64_t length) {
  HammerPage page = {.markdown = markdown, .markdown_length = length};
  // In a page, a length of 0 reads up to the NUL, which markdown need not have.
  if (!length) page.markdown = "";
  hammer_put_page(out, &page, 0);
}

// Writes a reply that keeps the user on the page they were on, with what they entered and its state,
// showing each error under its field. With no errors, or no page to keep, the action ends.
static inline void hammer_put_errors(HammerBuffer *out, const HammerError *errors, uint32_t count, const char *message) {
  hammer_put_text(out, "{\"errors\":{");
  for (uint32_t i = 0; i < count; i++) {
    if (i) hammer_put_char(out, ',');
    hammer_put_json_text(out, errors[i].field ? errors[i].field : "");
    hammer_put_char(out, ':');
    hammer_put_json_text(out, errors[i].message);
  }
  hammer_put_char(out, '}');
  hammer_put_reply_end(out, message, 0);
}

// Writes a reply that ends the action and opens an item: place is project, scene, note, entry, or event,
// and id the item's, when has_id is set (never for project).
static inline void hammer_put_open(HammerBuffer *out, const char *place, int64_t id, int has_id, const char *message) {
  hammer_put_text(out, "{\"open\":{\"place\":");
  hammer_put_json_text(out, place);
  if (has_id) {
    hammer_put_text(out, ",\"id\":");
    hammer_put_int(out, id);
  }
  hammer_put_char(out, '}');
  hammer_put_reply_end(out, message, 0);
}

#endif // HAMMER_ACTION_H
