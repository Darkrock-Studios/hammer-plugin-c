#!/bin/sh
# Runs test.c: the plugin built for this machine with HAMMER_NATIVE and the address and undefined
# behavior sanitizers, reading resources from resources/. Needs clang.
set -e
cd "$(dirname "$0")"
mkdir -p build
clang -DHAMMER_NATIVE -std=c11 -g -O1 -Wall -Wextra -Werror -Wno-unused-function -I include \
	-fsanitize=address,undefined -fno-sanitize-recover=all \
	-o build/test test.c
HAMMER_RESOURCES=resources build/test
