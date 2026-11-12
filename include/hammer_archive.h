// An archive exporter's reply. For a format whose manifest entry says `output = "archive"`, the `export`
// export returns {"files": [{"path": "story.md", "text": "..."}, {"path": "images/5.png", "image": 5}]},
// and Hammer zips the files, in order, into the one export file. A file is text, bytes as base64
// "content", or an "image": the id of one of the request's "images", whose bytes Hammer writes itself, so
// the reply carries none. Include it after hammer.h.
//
//   HammerArchive archive = {0};
//   hammer_archive_text(&archive, "story.md", story, story_length);
//   hammer_archive_image(&archive, "images/5.png", 5); // an export image's "id"
//   uint64_t length;
//   const char *reply = hammer_archive_finish(&archive, &length);
//   extism_output_buf(reply, length);
//
// A path is relative, with `/` between its segments: not empty, not starting with `/`, no `\`, no empty
// segment (so no `//` or trailing `/`), no `.` or `..` segment, and not a path already added. Adding a
// file at any other path calls hammer_fail with a message naming it, as Hammer would refuse the reply.
// A path is NUL-terminated while text and base64 take a length, so a path made from hammer_json_string's
// raw contents must be copied and terminated first.
// Like a HammerBuffer, an archive lives in hammer_alloc's memory, so one kept between exports must be
// zeroed before reuse.
#ifndef HAMMER_ARCHIVE_H
#define HAMMER_ARCHIVE_H

#include "hammer.h"
#include "hammer_base64.h"
#include "hammer_out.h"

typedef struct {
  // The reply so far: empty until a file is added.
  HammerBuffer json;
  // Each path added, as its length's 8 bytes and then its own, for refusing one added twice.
  HammerBuffer paths;
  // Set by hammer_archive_finish, after which no file can be added.
  int finished;
} HammerArchive;

// The rule `length` bytes of path break, such as "has an empty segment", or 0 when they break none.
// Whether the path was added already is hammer_archive_has's to say.
static inline const char *hammer_archive_path_error(const char *path, uint64_t length) {
  if (!length) return "is empty";
  if (path[0] == '/') return "starts with /";
  for (uint64_t i = 0; i < length; i++) {
    if (path[i] == '\\') return "has a backslash";
  }
  for (uint64_t start = 0; start <= length;) {
    uint64_t end = start;
    while (end < length && path[end] != '/') end++;
    if (end == start) return "has an empty segment";
    if (end - start == 1 && path[start] == '.') return "has a . segment";
    if (end - start == 2 && path[start] == '.' && path[start + 1] == '.') return "has a .. segment";
    start = end + 1;
  }
  return 0;
}

// Whether the archive has a file at `length` bytes of path already.
static inline int hammer_archive_has(const HammerArchive *archive, const char *path, uint64_t length) {
  for (uint64_t at = 0; at < archive->paths.length;) {
    uint64_t n;
    memcpy(&n, archive->paths.data + at, sizeof n);
    at += sizeof n;
    if (n == length) {
      uint64_t i = 0;
      while (i < n && archive->paths.data[at + i] == path[i]) i++;
      if (i == n) return 1;
    }
    at += n;
  }
  return 0;
}

// Checks path and writes the file's entry up to the value of `key`.
static inline void hammer_archive_start(HammerArchive *archive, const char *path, const char *key) {
  if (archive->finished) hammer_fail("A file was added to an archive after hammer_archive_finish");
  uint64_t length = strlen(path);
  const char *rule = hammer_archive_path_error(path, length);
  if (!rule && hammer_archive_has(archive, path, length)) rule = "is already in the archive";
  if (rule) {
    HammerBuffer message = {0};
    hammer_put_text(&message, "Archive path ");
    hammer_put_json_string(&message, path, length);
    hammer_put_char(&message, ' ');
    hammer_put_text(&message, rule);
    hammer_fail(hammer_terminated(&message));
  }
  hammer_put(&archive->paths, (const char *)&length, sizeof length);
  hammer_put(&archive->paths, path, length);
  hammer_put_text(&archive->json, archive->json.length ? ",{\"path\":" : "{\"files\":[{\"path\":");
  hammer_put_json_string(&archive->json, path, length);
  hammer_put_text(&archive->json, ",\"");
  hammer_put_text(&archive->json, key);
  hammer_put_text(&archive->json, "\":");
}

// Adds `length` bytes of UTF-8 text as the file at path.
static inline void hammer_archive_text(HammerArchive *archive, const char *path, const char *text, uint64_t length) {
  hammer_archive_start(archive, path, "text");
  hammer_put_json_string(&archive->json, text, length);
  hammer_put_char(&archive->json, '}');
}

// Adds `length` bytes as the file at path, encoded as base64.
static inline void hammer_archive_bytes(HammerArchive *archive, const char *path, const unsigned char *bytes,
                                        uint64_t length) {
  hammer_archive_start(archive, path, "content");
  uint64_t size = hammer_base64_encoded_size(length);
  hammer_reserve(&archive->json, size + 3);
  hammer_put_char(&archive->json, '"');
  hammer_base64_encode(bytes, length, archive->json.data + archive->json.length);
  archive->json.length += size;
  hammer_put_text(&archive->json, "\"}");
}

// Adds the file at path whose bytes `length` characters of standard padded base64 hold, written as they
// are, for base64 a plugin built or read from a resource. Hammer fails the export when they are not
// base64.
static inline void hammer_archive_base64(HammerArchive *archive, const char *path, const char *text,
                                         uint64_t length) {
  hammer_archive_start(archive, path, "content");
  hammer_put_char(&archive->json, '"');
  hammer_put(&archive->json, text, length);
  hammer_put_text(&archive->json, "\"}");
}

// Adds image `id` of the request's "images" as the file at path. Hammer writes its bytes, so the reply
// carries none; an id it did not send fails the export.
static inline void hammer_archive_image(HammerArchive *archive, const char *path, int32_t id) {
  hammer_archive_start(archive, path, "image");
  hammer_put_int(&archive->json, id);
  hammer_put_char(&archive->json, '}');
}

// Ends the reply, once every file is added, and returns it for extism_output_buf, setting *length; a
// second call returns it again. Calls hammer_fail when no file was added.
static inline const char *hammer_archive_finish(HammerArchive *archive, uint64_t *length) {
  if (!archive->json.length) hammer_fail("An archive needs at least one file");
  if (!archive->finished) hammer_put_text(&archive->json, "]}");
  archive->finished = 1;
  *length = archive->json.length;
  return archive->json.data;
}

#endif // HAMMER_ARCHIVE_H
