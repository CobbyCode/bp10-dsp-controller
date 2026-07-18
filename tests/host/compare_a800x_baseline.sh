#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
out_dir="$repo_dir/build/host-tests"
baseline_dir="$(mktemp -d)"
trap 'rm -rf "$baseline_dir"' EXIT
git -C "$repo_dir" show 3e0fe0c:main/mvs_protocol.c > "$baseline_dir/mvs_protocol.c"
git -C "$repo_dir" show 3e0fe0c:main/mvs_protocol.h > "$baseline_dir/mvs_protocol.h"
cc -std=c11 -Wall -Wextra -Werror -include stddef.h \
  -I"$repo_dir/tests/host/include" -I"$baseline_dir" \
  "$repo_dir/tests/host/dump_a800x_frames.c" "$baseline_dir/mvs_protocol.c" \
  -o "$out_dir/a800x_baseline_dump"
cc -std=c11 -Wall -Wextra -Werror -include stddef.h \
  -I"$repo_dir/tests/host/include" -I"$repo_dir/main" \
  "$repo_dir/tests/host/dump_a800x_frames.c" "$repo_dir/main/mvs_protocol.c" \
  -lm -o "$out_dir/a800x_current_dump"
"$out_dir/a800x_baseline_dump" > "$out_dir/a800x_baseline.frames"
"$out_dir/a800x_current_dump" > "$out_dir/a800x_current.frames"
cmp "$out_dir/a800x_baseline.frames" "$out_dir/a800x_current.frames"
echo "a800x_frame_baseline_compare: PASS"
