#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -fno-sanitize-recover=all -I test-shim control.c test_control.c -o "$out/test_control"
"$out/test_control"
