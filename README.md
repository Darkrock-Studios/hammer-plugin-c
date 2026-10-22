# Hammer plugin template (C)

A starting point for a [Hammer](https://hammer.ink) plugin written in C. It builds a working plugin,
**Word count**, which adds a "Count words" item to Project Home's and the scene editor's menus and replies
with each scene's word count. Replace it with your own.

## Make it yours

1. Choose **Use this template** on GitHub to make your own repository from this one.
2. Rename the plugin. In `manifest.toml`, set `id` (lowercase, and unique among Hammer's plugins), `name`,
   and `description`; the package is named after the `id`.
3. Ask for what your plugin needs. `[permissions] operations` in `manifest.toml` lists the operations it
   may call; the writer sees that list when they install it.
4. Replace `plugin.c` with your plugin's logic, and `test.c` with its tests. It calls Hammer with
   `hammer_call("scene.read", input_json)`, and reads and writes JSON with the kit's `hammer_json.h` and
   `hammer_out.h`.
5. Choose a license. This template is under 0BSD, so you can use it however you like without keeping
   its notice; replace `LICENSE` with your plugin's own. The kit in `include/` keeps its own licenses.

## Build and test

You need clang, lld (for `wasm-ld`), and `zip`; on Debian or Ubuntu, `sudo apt install clang lld zip`.

```sh
./test.sh     # run the tests on your machine
./build.sh    # build build/word-count.hammerplugin
```

Install the package in Hammer on desktop: Settings, Plugins, Install plugin. Pick the `.hammerplugin`,
approve its permissions, and restart Hammer.

## The kit

C has no package registry, so the kit's headers live in `include/`, copied from
[hammer-plugin-sdk](https://github.com/Darkrock-Studios/hammer-plugin-sdk) 0.6.0. It's header-only:
`hammer.h` documents every export, and the other headers say what they do at their top. To move to a newer
kit, replace them with a release's, such as 0.7.0's:

```sh
curl -sSfL https://github.com/Darkrock-Studios/hammer-plugin-sdk/archive/refs/tags/v0.7.0.tar.gz \
  | tar xz -C include --strip-components=3 hammer-plugin-sdk-0.7.0/c/include
```

## Layout

| Path | What |
| --- | --- |
| `manifest.toml` | What the plugin adds to Hammer, and the operations it may call |
| `plugin.c` | The plugin: the `action` export Hammer calls, and its logic |
| `test.c` | Native tests of the logic, built with `HAMMER_NATIVE`, where Hammer's operations aren't available |
| `include/` | The kit: Hammer's headers on top of Extism's `extism-pdk.h`, with their licenses |
| `build.sh`, `test.sh` | Build the module for wasm32 and pack it with the manifest; build and run the tests |
| `.github/workflows/build.yml` | Tests and packages every push; a `v*` tag makes a GitHub release with the `.hammerplugin` |

## Learn more

- [The Hammer developer site](https://plugins.hammer.ink): the API reference, with the manifest, every
  kind of extension, and all of Hammer's operations.
- [Recipes](https://github.com/Darkrock-Studios/hammer-plugin-sdk/blob/main/RECIPES.md): a small example of
  each kind of plugin.
- [Hammer's own plugins](https://github.com/Darkrock-Studios/hammer-plugins), fuller examples, among them
  `simple-grammar`, `style`, and `plaintext` in C.
