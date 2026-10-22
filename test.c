// The plugin's native unit tests; ./test.sh builds and runs them.
#include "plugin.c"
#include "hammer_test.h"

static const char *story =
    "{\"nodes\":[{\"id\":1,\"name\":\"Part One\",\"kind\":\"group\",\"children\":["
    "{\"id\":2,\"name\":\"Storm\",\"kind\":\"scene\",\"children\":[]},"
    "{\"id\":3,\"name\":\"Calm \\\"after\\\"\",\"kind\":\"scene\",\"children\":[]}]}]}";

static int is(const char *data, uint64_t length, const char *expected) {
  return length == strlen(expected) && memcmp(data, expected, length) == 0;
}

static Scenes pick_from_story(const int64_t *chosen, uint64_t chosen_count) {
  HammerJson tree;
  hammer_json_init(&tree, story, strlen(story));
  return pick(&tree, chosen, chosen_count);
}

static void picks_every_scene_or_those_chosen(void) {
  Scenes all = pick_from_story(0, 0);
  HAMMER_CHECK(all.count == 2);
  HAMMER_CHECK(is(all.items[0].name, all.items[0].name_length, "Storm"));
  HAMMER_CHECK(is(all.items[1].name, all.items[1].name_length, "Calm \"after\""));

  int64_t chosen[] = {3};
  Scenes calm = pick_from_story(chosen, 1);
  HAMMER_CHECK(calm.count == 1 && calm.items[0].id == 3);
}

static void reports_each_scene_with_a_total(void) {
  Scene items[] = {{2, "Storm", 5, 3}, {3, "Calm", 4, 1}};
  HammerBuffer both = {0}, one = {0};
  report(&both, &(Scenes){items, 2, 2});
  report(&one, &(Scenes){items, 1, 1});
  HAMMER_CHECK(is(both.data, both.length, "# Word count\n\n- Storm: 3\n- Calm: 1\n\n**Total: 4**\n"));
  HAMMER_CHECK(is(one.data, one.length, "# Word count\n\n- Storm: 3\n"));
}

static void words_are_runs_with_letters_or_digits(void) {
  const char *text = "She said \xE2\x80\x94 twice \xE2\x80\x94 \xE2\x80\x9Cno.\xE2\x80\x9D";
  HAMMER_CHECK(words_in(text, strlen(text)) == 4);
  const char *mixed = "In 1999, twenty-one caf\xC3\xA9s.";
  HAMMER_CHECK(words_in(mixed, strlen(mixed)) == 4);
  HAMMER_CHECK(words_in("", 0) == 0);
}

int main(void) {
  picks_every_scene_or_those_chosen();
  reports_each_scene_with_a_total();
  words_are_runs_with_letters_or_digits();
  return hammer_test_summary();
}
