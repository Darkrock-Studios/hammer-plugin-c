// Word count: counts the words in each scene, for the whole story or the scenes picked, and replies with
// a document the writer can copy or save as a note.

#define HAMMER_IMPLEMENTATION
#include "hammer.h"
#include "hammer_action.h"

typedef struct {
  int64_t id;
  char *name;
  uint64_t name_length;
  uint64_t words;
} Scene;

typedef struct {
  Scene *items;
  uint64_t count;
  uint64_t capacity;
} Scenes;

static void add_scene(Scenes *scenes, Scene scene) {
  if (scenes->count == scenes->capacity) {
    uint64_t capacity = scenes->capacity ? scenes->capacity * 2 : 64;
    Scene *grown = hammer_must_alloc(capacity * sizeof(Scene));
    if (scenes->count) memcpy(grown, scenes->items, scenes->count * sizeof(Scene));
    scenes->items = grown;
    scenes->capacity = capacity;
  }
  scenes->items[scenes->count++] = scene;
}

static int is_chosen(const int64_t *chosen, uint64_t chosen_count, int64_t id) {
  if (!chosen_count) return 1;
  for (uint64_t i = 0; i < chosen_count; i++) {
    if (chosen[i] == id) return 1;
  }
  return 0;
}

// The scenes of a scene.tree node list, in story order with groups opened up: those in chosen, or all
// of them when chosen_count is 0.
static void pick_nodes(HammerJson *json, const int64_t *chosen, uint64_t chosen_count, Scenes *scenes) {
  if (!hammer_json_enter_array(json)) return;
  while (hammer_json_next_item(json)) {
    const char *key, *name = 0;
    uint64_t key_length, name_length = 0, children_at = 0;
    int64_t id = 0;
    int is_scene = 0;
    if (!hammer_json_enter_object(json)) return;
    while (hammer_json_next_key(json, &key, &key_length)) {
      const char *raw;
      uint64_t raw_length;
      if (hammer_json_key_is(key, key_length, "id")) {
        id = hammer_json_int(json);
      } else if (hammer_json_key_is(key, key_length, "name")) {
        hammer_json_string(json, &name, &name_length);
      } else if (hammer_json_key_is(key, key_length, "kind") && hammer_json_string(json, &raw, &raw_length)) {
        is_scene = hammer_json_key_is(raw, raw_length, "scene");
      } else if (hammer_json_key_is(key, key_length, "children")) {
        // Read after the node's other keys, which may come later.
        hammer_json_peek(json);
        children_at = json->at;
        hammer_json_skip(json);
      } else {
        hammer_json_skip(json);
      }
    }
    if (is_scene && is_chosen(chosen, chosen_count, id)) {
      char *decoded = hammer_must_alloc(name_length + 1);
      uint64_t decoded_length = name ? hammer_json_unescape(name, name_length, decoded) : 0;
      add_scene(scenes, (Scene){id, decoded, decoded_length, 0});
    } else if (!is_scene && children_at) {
      uint64_t resume = json->at;
      json->at = children_at;
      pick_nodes(json, chosen, chosen_count, scenes);
      json->at = resume;
    }
  }
}

// The scenes picked from scene.tree's output, {"nodes": [...]}.
static Scenes pick(HammerJson *tree, const int64_t *chosen, uint64_t chosen_count) {
  Scenes scenes = {0};
  const char *key;
  uint64_t key_length;
  if (!hammer_json_enter_object(tree)) return scenes;
  while (hammer_json_next_key(tree, &key, &key_length)) {
    if (hammer_json_key_is(key, key_length, "nodes")) pick_nodes(tree, chosen, chosen_count, &scenes);
    else hammer_json_skip(tree);
  }
  return scenes;
}

static int is_space(unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

// Words are runs between spaces that hold at least one letter or digit, so dashes and stray punctuation
// don't count.
static uint64_t words_in(const char *text, uint64_t length) {
  HammerText all = {text, length};
  uint64_t words = 0;
  int counted = 0;
  for (uint64_t i = 0; i < length; i += hammer_char_length(all, i)) {
    if (is_space((unsigned char)text[i])) {
      counted = 0;
    } else if (!counted && (hammer_letter_at(all, i) || hammer_is_digit(text[i]))) {
      words++;
      counted = 1;
    }
  }
  return words;
}

static void report(HammerBuffer *out, const Scenes *scenes) {
  uint64_t total = 0;
  hammer_put_text(out, "# Word count\n\n");
  for (uint64_t i = 0; i < scenes->count; i++) {
    const Scene *scene = &scenes->items[i];
    hammer_put_text(out, "- ");
    hammer_put(out, scene->name, scene->name_length);
    hammer_put_text(out, ": ");
    hammer_put_int(out, (int64_t)scene->words);
    hammer_put_char(out, '\n');
    total += scene->words;
  }
  if (scenes->count > 1) {
    hammer_put_text(out, "\n**Total: ");
    hammer_put_int(out, (int64_t)total);
    hammer_put_text(out, "**\n");
  }
}

// Hammer's operations, so none of this is in a native test build.
#ifndef HAMMER_NATIVE

static const char *failure;

// Runs an operation, input being its input object's JSON with a NUL after it. Returns 1 with output at
// the reply's output, or 0 with failure set.
static int call(const char *operation, const char *input, HammerJson *output) {
  ExtismHandle handle = hammer_call(operation, input);
  uint64_t size = extism_length(handle);
  char *reply = hammer_must_alloc(size);
  extism_load_from_handle(handle, 0, reply, size);
  extism_free(handle);

  failure = "Hammer sent a reply Word count could not read";
  hammer_json_init(output, reply, size);
  const char *key, *raw;
  uint64_t key_length, raw_length;
  if (!hammer_json_enter_object(output)) return 0;
  while (hammer_json_next_key(output, &key, &key_length)) {
    if (hammer_json_key_is(key, key_length, "output")) return 1;
    if (hammer_json_key_is(key, key_length, "error") && hammer_json_enter_object(output)) {
      while (hammer_json_next_key(output, &key, &key_length)) {
        if (hammer_json_key_is(key, key_length, "message") && hammer_json_string(output, &raw, &raw_length)) {
          char *message = hammer_must_alloc(raw_length + 1);
          message[hammer_json_unescape(raw, raw_length, message)] = 0;
          failure = message;
        } else {
          hammer_json_skip(output);
        }
      }
      return 0;
    }
    hammer_json_skip(output);
  }
  return 0;
}

// The input scene.tree and scene.read take: the project, and a scene's id when has_id is set.
static const char *scene_input(HammerText project, int64_t id, int has_id) {
  HammerBuffer input = {0};
  // The request's project is still escaped, as it was in its JSON.
  hammer_put_text(&input, "{\"project\":\"");
  hammer_put(&input, project.data, project.length);
  hammer_put_char(&input, '"');
  if (has_id) {
    hammer_put_text(&input, ",\"id\":");
    hammer_put_int(&input, id);
  }
  hammer_put_text(&input, "}");
  hammer_put_char(&input, 0);
  return input.data;
}

// The words in scene.read's output, {"markdown": "...", ...}.
static int count_scene(HammerJson *json, uint64_t *words) {
  const char *key, *raw;
  uint64_t key_length, raw_length;
  if (!hammer_json_enter_object(json)) return 0;
  while (hammer_json_next_key(json, &key, &key_length)) {
    if (hammer_json_key_is(key, key_length, "markdown") && hammer_json_string(json, &raw, &raw_length)) {
      char *markdown = hammer_must_alloc(raw_length);
      *words = words_in(markdown, hammer_json_unescape(raw, raw_length, markdown));
      return 1;
    }
    hammer_json_skip(json);
  }
  return 0;
}

static void reply(const HammerBuffer *out) { extism_output_buf(out->data, out->length); }

// Counts the scenes picked, or the scene it was run from, or else the whole story.
static int count_words(const HammerActionRequest *request) {
  HammerBuffer chosen = {0};
  HammerJson json;
  if (hammer_input(request, "scenes", &json) && hammer_json_enter_array(&json)) {
    while (hammer_json_next_item(&json)) {
      int64_t id = hammer_json_int(&json);
      hammer_put(&chosen, (const char *)&id, sizeof(id));
    }
  }
  if (!chosen.length && hammer_text_is(request->place, "scene") && request->has_id) {
    hammer_put(&chosen, (const char *)&request->id, sizeof(request->id));
  }

  HammerJson tree;
  if (!call("scene.tree", scene_input(request->project, 0, 0), &tree)) return 0;
  Scenes scenes = pick(&tree, (const int64_t *)chosen.data, chosen.length / sizeof(int64_t));
  HammerBuffer out = {0};
  if (!scenes.count) {
    hammer_put_message(&out, "There are no scenes to count");
    reply(&out);
    return 1;
  }

  for (uint64_t i = 0; i < scenes.count; i++) {
    Scene *scene = &scenes.items[i];
    HammerBuffer status = {0};
    hammer_put_text(&status, "Counting ");
    hammer_put(&status, scene->name, scene->name_length);
    hammer_put_char(&status, 0);
    hammer_progress((double)i / (double)scenes.count, status.data);

    HammerJson text;
    if (!call("scene.read", scene_input(request->project, scene->id, 1), &text)) return 0;
    if (!count_scene(&text, &scene->words)) return 0;
  }

  HammerBuffer markdown = {0};
  report(&markdown, &scenes);
  HammerPage page = {.title = "Word count", .markdown = markdown.data, .markdown_length = markdown.length};
  hammer_put_page(&out, &page, 0);
  reply(&out);
  return 1;
}

// Hammer calls this for every action in the manifest; tell them apart by name.
int32_t EXTISM_EXPORTED_FUNCTION(action) {
  hammer_alloc_reset();
  uint64_t size = extism_input_length();
  char *input = hammer_must_alloc(size);
  extism_load_input(0, input, size);

  HammerActionRequest request;
  if (!hammer_action_read(input, size, &request)) {
    extism_error_set_buf_from_sz("Hammer sent a request Word count could not read");
    return 1;
  }
  if (hammer_text_is(request.action, "count") && !count_words(&request)) {
    extism_error_set_buf_from_sz(failure);
    return 1;
  }
  return 0;
}

#endif // HAMMER_NATIVE
