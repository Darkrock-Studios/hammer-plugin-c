// Hammer's plugin kit for C, on top of Extism's C PDK (extism-pdk.h).
//
// In exactly one source file, define HAMMER_IMPLEMENTATION before including this header. It also
// defines EXTISM_IMPLEMENTATION, so don't define that yourself. Without EXTISM_USE_LIBC, it also
// provides the memset, memcpy, and strlen that clang expects even from freestanding code.
//
// With HAMMER_NATIVE defined, a plugin compiles for the machine building it instead, with libc and
// without Extism, so its logic can be unit tested natively (see hammer_test.h and c/test.sh). Only
// memory, hammer_fail, resources, and what builds on them are there: no operations or cache.
//
// The rest of the kit: hammer_json.h reads JSON, hammer_out.h writes output, hammer_text.h finds
// words in UTF-8 text, hammer_action.h reads an `action` export's request and writes its replies, and
// hammer_diagnose.h does a `diagnose` export's reading and writing.
//
// Exports a plugin can provide. Each is also given "locale": the UI's BCP 47 tag, such as "fr-CA", or
// null, for replying in the user's language. The manifest's and settings' words are translated by
// Hammer itself, from the package's locales/<tag>.toml.
//   export  Renders every export format in the manifest. Input is JSON:
//           {"format": "<id>", "projectName": "...", "language": "en", "topLevelAsChapters": true,
//            "chapters": [{"name": "...", "scenes": ["<markdown>", ...], "prose": []}],
//            "settings": {"<key>": <value>, ...}}
//           A format whose manifest entry says `input = "prose"` gets each scene in `prose`
//           instead, as the host parsed it: a list of blocks, each with a "type" of paragraph,
//           blank, heading, list, quote, code, rule, or table. Text comes in spans:
//           {"text": "...", "italic": true}, with bold, strikethrough, code, and link likewise
//           left out when unset. hammer_json.h reads either form. Output is the file's bytes.
//   action  Runs an action the manifest declares under [[actions]], from the menu of a screen in
//           its `places` (project, scene, note, entry, event). Input is JSON:
//           {"action": "<name>", "project": "<name>", "settings": {...}, "input": {...},
//            "context": {"place": "scene", "id": 12}, "button": null, "changed": null,
//            "state": null, "resumed": false}. "context" is the screen and the id of the item it
//           shows (null on the project's). The first call has no button, and "input" has a value for
//           each of the action's [[actions.field]]s. Output is a reply object, every key optional:
//           {"page": {"title": "...", "markdown": "...", "fields": [...], "buttons": [...],
//           "state": ...}, "message": "...", "errors": {"<field>": "..."},
//           "open": {"place": "entry", "id": 12}}. A page is shown in the action's dialog; pressing
//           one of its buttons calls the action again with the button's id, the page's values as
//           "input", and the page's "state". A message is shown briefly. "errors" keep the user on
//           the page they were on, "open" ends the action and goes to an item, and a reply with none
//           of page, errors, or open, or no output at all, ends it. hammer_action.h reads the request
//           and writes replies. Report progress on a long run with hammer_progress.
//   command Answers one line of standard input to a command the manifest declares under
//           [[commands]]. Input is JSON: {"command": "<name>", "args": [...], "settings": {...},
//           "line": "..."}. Output, if any, is written as one line of standard output.
//   diagnose Checks paragraphs the editor sends for a check the manifest declares under
//           [[diagnostics]]. Input is JSON: {"diagnostics": "<name>", "paragraphs": ["...", ...],
//           "language": "en" or null, "settings": {...}}, paragraphs as plain text. Output is
//           {"diagnostics": [{"paragraph": 0, "start": 4, "end": 11, "message": "...",
//           "fixes": ["..."]}]}, start and end being UTF-8 byte offsets into the paragraph. A fix is a
//           replacement, shown as itself, or {"replacement": "", "label": "Remove “the”"} where the
//           replacement alone would not say what it does.
#ifndef HAMMER_H
#define HAMMER_H

#ifdef HAMMER_NATIVE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#else
#ifdef HAMMER_IMPLEMENTATION
#define EXTISM_IMPLEMENTATION
#endif
#include "extism-pdk.h"
#endif

// The host API version this header targets; the manifest's `api` must match.
#define HAMMER_API_VERSION 1

// What hammer_must_alloc reports; define it before including this header to say it for your plugin.
#ifndef HAMMER_OUT_OF_MEMORY
#define HAMMER_OUT_OF_MEMORY "The plugin ran out of memory"
#endif

// Reports how far an action has got: fraction from 0 to 1, or negative when not known, and a line
// saying what it is doing, or 0. Hammer shows it while the action runs. When the user has stopped the
// run, the call does not return: the action ends there. Does nothing outside an action.
void hammer_progress(double fraction, const char *message);

// Scratch memory from the top of the module's linear memory, grown as needed. Nothing is freed on its
// own: call hammer_alloc_reset() at the start of each export to reuse it. Returns 0 when the host's
// memory limit is reached.
void *hammer_alloc(uint64_t size);
void hammer_alloc_reset(void);

// Keeps everything hammer_alloc has given out so far, for later calls: resets go back only to here.
// Memory kept is never reused, so keep what later calls need, such as a resource, early in a call.
void hammer_alloc_keep(void);

// Ends the call with message as its error.
__attribute__((noreturn)) void hammer_fail(const char *message);

// hammer_alloc, or hammer_fail with HAMMER_OUT_OF_MEMORY.
void *hammer_must_alloc(uint64_t size);

// Reads a file from the package's resources/ by its path there, such as "words/en.txt", into
// hammer_alloc's memory with a NUL after it, and sets *length. Returns 0 when the package has none by
// that name, or it is empty. Hammer reads it from the package each time, so one used on every call is
// worth keeping with hammer_alloc_keep. Natively, it reads from the directory in HAMMER_RESOURCES,
// which c/test.sh sets to the plugin's resources/.
const char *hammer_resource_load(const char *name, uint64_t *length);

// A resource that is a list, such as a word list, read with hammer_resource_load: its lines, each
// trimmed of spaces, tabs, and a \r, without blank lines and # comments, ending with a 0. Sets *count
// to how many. Returns 0 when the package has no such file, or it is empty.
const char **hammer_resource_lines(const char *name, uint64_t *count);

#ifndef HAMMER_NATIVE

// Raw progress report: a handle to {"fraction": 0.5, "message": "..."}, either key optional.
EXTISM_IMPORT_USER("hammer_progress")
extern void hammer_progress_report(ExtismHandle report);

// Raw resource access: a handle to the resource the handle name names, or 0 when there is none.
EXTISM_IMPORT_USER("hammer_resource")
extern ExtismHandle hammer_resource(ExtismHandle name);

// Raw dispatch: request is a handle to {"operation": "<name>", "input": {...}}.
EXTISM_IMPORT_USER("hammer_dispatch")
extern ExtismHandle hammer_dispatch(ExtismHandle request);

// The plugin's own cache, kept by Hammer between calls and runs. Keys and values are bytes. Hammer
// may drop any value at any time: it keeps the most recently written, up to a total size, and clears
// them all when the plugin is installed again or uninstalled. Nothing in it is synced or backed up.
// A call still running on the old version during an install may write after the clear, so put a
// version in keys whose values' format may change.
#define HAMMER_CACHE_MAX_KEY 1024
#define HAMMER_CACHE_MAX_VALUE (1024 * 1024)

// Raw cache access: a handle to the key's value, or 0 when it has none.
EXTISM_IMPORT_USER("hammer_cache_get")
extern ExtismHandle hammer_cache_get(ExtismHandle key);

// Raw cache access: stores value under key, or removes the key when value is 0, as it is for an empty
// value. A key or value over its limit ends the call with an error.
EXTISM_IMPORT_USER("hammer_cache_set")
extern void hammer_cache_set(ExtismHandle key, ExtismHandle value);

// Runs a Hammer operation the manifest has permission for. input_json is the operation's input
// object, e.g. {"project":"My Novel","id":12}. Returns a handle to the reply, one of:
//   {"output": {...}}
//   {"error": {"kind": "NotFound" | "InvalidInput" | "PermissionDenied" | "Failed", "message": "..."}}
ExtismHandle hammer_call(const char *operation, const char *input_json);

// A handle to the value cached under key, or 0 when there is none. Free it with extism_free.
ExtismHandle hammer_cache_read(const void *key, uint64_t key_length);

// Caches length bytes of value under key; an empty value removes it. Returns 0, storing nothing, when
// either is over its limit.
int hammer_cache_write(const void *key, uint64_t key_length, const void *value, uint64_t length);
#endif // HAMMER_NATIVE

#ifdef HAMMER_IMPLEMENTATION
void *hammer_must_alloc(uint64_t size) {
  void *memory = hammer_alloc(size);
  if (!memory) hammer_fail(HAMMER_OUT_OF_MEMORY);
  return memory;
}

const char **hammer_resource_lines(const char *name, uint64_t *count) {
  *count = 0;
  uint64_t length;
  // The loaded file is this call's, so it is cut into lines where it is.
  char *text = (char *)hammer_resource_load(name, &length);
  if (!text) return 0;
  uint64_t most = 1;
  for (uint64_t i = 0; i < length; i++) most += text[i] == '\n';
  const char **lines = hammer_must_alloc((most + 1) * sizeof(*lines));
  char *line = text;
  while (*line) {
    char *end = line;
    while (*end && *end != '\n') end++;
    char *next = *end ? end + 1 : end;
    while (end > line && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) end--;
    *end = 0;
    while (*line == ' ' || *line == '\t') line++;
    if (*line && *line != '#') lines[(*count)++] = line;
    line = next;
  }
  lines[*count] = 0;
  return lines;
}

#ifdef HAMMER_NATIVE
// As much as a plugin gets by default.
#define HAMMER_NATIVE_MEMORY (64ull * 1024 * 1024)
static unsigned char *hammer_native_heap;
static uint64_t hammer_native_used;
static uint64_t hammer_native_kept;

void hammer_alloc_reset(void) { hammer_native_used = hammer_native_kept; }

void hammer_alloc_keep(void) { hammer_native_kept = hammer_native_used; }

void *hammer_alloc(uint64_t size) {
  if (!hammer_native_heap) hammer_native_heap = malloc(HAMMER_NATIVE_MEMORY);
  uint64_t at = (hammer_native_used + 7) & ~7ull;
  if (!hammer_native_heap || size > HAMMER_NATIVE_MEMORY - at) return 0;
  hammer_native_used = at + size;
  return hammer_native_heap + at;
}

void hammer_fail(const char *message) {
  fprintf(stderr, "hammer_fail: %s\n", message);
  abort();
}

void hammer_progress(double fraction, const char *message) {
  (void)fraction;
  (void)message;
}

// Whether name stays inside the directory it is looked up in, as Hammer's names under resources/ do.
static int hammer_native_resource_name(const char *name) {
  for (const char *segment = name;;) {
    const char *slash = strchr(segment, '/');
    size_t n = slash ? (size_t)(slash - segment) : strlen(segment);
    if (n == 0 || (n == 1 && segment[0] == '.') || (n == 2 && segment[0] == '.' && segment[1] == '.')) return 0;
    if (!slash) return 1;
    segment = slash + 1;
  }
}

const char *hammer_resource_load(const char *name, uint64_t *length) {
  *length = 0;
  const char *directory = getenv("HAMMER_RESOURCES");
  char path[4096];
  struct stat info;
  if (!directory || !hammer_native_resource_name(name)) return 0;
  if (snprintf(path, sizeof path, "%s/%s", directory, name) >= (int)sizeof path) return 0;
  if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0) return 0;
  FILE *file = fopen(path, "rb");
  if (!file) return 0;
  long size = (long)info.st_size;
  char *bytes = hammer_must_alloc((uint64_t)size + 1);
  if (fread(bytes, 1, (size_t)size, file) == (size_t)size) {
    bytes[size] = 0;
    *length = (uint64_t)size;
  } else {
    bytes = 0;
  }
  fclose(file);
  return bytes;
}
#else
void hammer_fail(const char *message) {
  extism_error_set_buf_from_sz(message);
  __builtin_trap();
}

#ifndef EXTISM_USE_LIBC
// Without libc, clang still emits calls to these for plain loops and struct copies.
void *memset(void *dest, int c, size_t n) {
  unsigned char *d = dest;
  while (n--) *d++ = (unsigned char)c;
  return dest;
}

void *memcpy(void *dest, const void *src, size_t n) {
  unsigned char *d = dest;
  const unsigned char *s = src;
  while (n--) *d++ = *s++;
  return dest;
}

size_t strlen(const char *s) {
  const char *end = s;
  while (*end) end++;
  return (size_t)(end - s);
}
#endif // EXTISM_USE_LIBC

extern unsigned char __heap_base;
static uintptr_t hammer_heap_top;
static uintptr_t hammer_heap_kept;

void hammer_alloc_reset(void) { hammer_heap_top = hammer_heap_kept ? hammer_heap_kept : (uintptr_t)&__heap_base; }

void hammer_alloc_keep(void) {
  if (hammer_heap_top == 0) hammer_alloc_reset();
  hammer_heap_kept = hammer_heap_top;
}

void *hammer_alloc(uint64_t size) {
  if (hammer_heap_top == 0) hammer_alloc_reset();
  uintptr_t at = (hammer_heap_top + 7) & ~(uintptr_t)7;
  uint64_t end = (uint64_t)at + size;
  uint64_t have = (uint64_t)__builtin_wasm_memory_size(0) * 65536;
  if (end > have && __builtin_wasm_memory_grow(0, (size_t)((end - have + 65535) / 65536)) == (size_t)-1) return 0;
  hammer_heap_top = (uintptr_t)end;
  return (void *)at;
}

void hammer_progress(double fraction, const char *message) {
  // Built on the stack, so reports use none of hammer_alloc's memory; a long message is cut.
  char report[512];
  uint64_t n = 0;
  static const char head[] = "{\"fraction\":";
  for (const char *c = head; *c; c++) report[n++] = *c;
  if (!(fraction >= 0)) { // negative, or NaN
    for (const char *c = "null"; *c; c++) report[n++] = *c;
  } else {
    uint32_t thousandths = fraction >= 1 ? 1000 : (uint32_t)(fraction * 1000);
    report[n++] = (char)('0' + thousandths / 1000);
    report[n++] = '.';
    report[n++] = (char)('0' + thousandths / 100 % 10);
    report[n++] = (char)('0' + thousandths / 10 % 10);
    report[n++] = (char)('0' + thousandths % 10);
  }
  if (message) {
    static const char key[] = ",\"message\":\"";
    for (const char *c = key; *c; c++) report[n++] = *c;
    for (const char *c = message; *c && n < sizeof(report) - 8; c++) {
      unsigned char b = (unsigned char)*c;
      if (b < 0x20) continue;
      if (b == '"' || b == '\\') report[n++] = '\\';
      report[n++] = (char)b;
    }
    report[n++] = '"';
  }
  report[n++] = '}';
  ExtismHandle handle = extism_alloc_buf(report, n);
  hammer_progress_report(handle);
  extism_free(handle);
}

ExtismHandle hammer_call(const char *operation, const char *input_json) {
  static const char head[] = "{\"operation\":\"";
  static const char middle[] = "\",\"input\":";
  static const char tail[] = "}";
  const uint64_t operation_len = extism_strlen(operation);
  const uint64_t input_len = extism_strlen(input_json);
  const uint64_t len = sizeof(head) - 1 + operation_len + sizeof(middle) - 1 + input_len + sizeof(tail) - 1;

  ExtismHandle request = extism_alloc(len);
  uint64_t at = 0;
  extism_store_to_handle(request, at, head, sizeof(head) - 1);
  at += sizeof(head) - 1;
  extism_store_to_handle(request, at, operation, operation_len);
  at += operation_len;
  extism_store_to_handle(request, at, middle, sizeof(middle) - 1);
  at += sizeof(middle) - 1;
  extism_store_to_handle(request, at, input_json, input_len);
  at += input_len;
  extism_store_to_handle(request, at, tail, sizeof(tail) - 1);

  ExtismHandle reply = hammer_dispatch(request);
  extism_free(request);
  return reply;
}

const char *hammer_resource_load(const char *name, uint64_t *length) {
  *length = 0;
  ExtismHandle name_handle = extism_alloc_buf(name, strlen(name));
  ExtismHandle handle = hammer_resource(name_handle);
  extism_free(name_handle);
  if (!handle) return 0;
  uint64_t n = extism_length(handle);
  char *bytes = hammer_must_alloc(n + 1);
  extism_load_from_handle(handle, 0, bytes, n);
  extism_free(handle);
  bytes[n] = 0;
  *length = n;
  return bytes;
}

ExtismHandle hammer_cache_read(const void *key, uint64_t key_length) {
  if (key_length > HAMMER_CACHE_MAX_KEY) return 0;
  ExtismHandle key_handle = extism_alloc_buf(key, key_length);
  ExtismHandle value = hammer_cache_get(key_handle);
  extism_free(key_handle);
  return value;
}

int hammer_cache_write(const void *key, uint64_t key_length, const void *value, uint64_t length) {
  if (key_length > HAMMER_CACHE_MAX_KEY || length > HAMMER_CACHE_MAX_VALUE) return 0;
  ExtismHandle key_handle = extism_alloc_buf(key, key_length);
  ExtismHandle value_handle = extism_alloc_buf(value, length);
  hammer_cache_set(key_handle, value_handle);
  extism_free(key_handle);
  extism_free(value_handle);
  return 1;
}
#endif // HAMMER_NATIVE
#endif // HAMMER_IMPLEMENTATION

#endif // HAMMER_H
