#!/bin/sh
# Builds build/<id>.hammerplugin, <id> being the manifest's: manifest.toml and the module, with
# settings.toml, locales/, and resources/ when the plugin has them. Needs clang, wasm-ld (lld), and zip.
set -e
cd "$(dirname "$0")"
id=$(sed -n 's/^id = "\(.*\)"/\1/p' manifest.toml | head -n 1)
mkdir -p build
# Freestanding wasm32: no libc or WASI, with the kit's headers in include/.
clang --target=wasm32-unknown-unknown -O2 -nostdlib -I include \
	-Wl,--no-entry -Wl,--export-dynamic \
	-o build/plugin.wasm plugin.c
rm -rf build/package
mkdir -p build/package
cp manifest.toml build/plugin.wasm build/package/
if [ -f settings.toml ]; then cp settings.toml build/package/; fi
for dir in locales resources; do
	if [ -d "$dir" ]; then cp -r "$dir" build/package/; fi
done
rm -f "build/$id.hammerplugin"
(cd build/package && zip -q -X -r "../$id.hammerplugin" .)
echo "Built build/$id.hammerplugin"
