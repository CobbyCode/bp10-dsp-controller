#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CobbyCode
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
out_dir="$repo_dir/build/host-tests"
fixture="$repo_dir/tests/host/fixtures/a800x_v0.4.0.frames"
mkdir -p "$out_dir"

cc -std=c11 -Wall -Wextra -Werror -include stddef.h \
  -I"$repo_dir/tests/host/include" -I"$repo_dir/main" \
  "$repo_dir/tests/host/dump_a800x_frames.c" \
  "$repo_dir/main/mvs_protocol.c" -lm \
  -o "$out_dir/a800x_current_dump"

"$out_dir/a800x_current_dump" > "$out_dir/a800x_current.frames"
cmp "$fixture" "$out_dir/a800x_current.frames"
echo "a800x_frame_fixture_compare: PASS"
