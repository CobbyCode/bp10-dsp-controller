#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
out_dir="$repo_dir/build/host-tests"
mkdir -p "$out_dir"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/tests/host/include" -I"$repo_dir/main" \
  "$repo_dir/tests/host/test_generic_acp.c" \
  "$repo_dir/main/mvs_protocol.c" \
  "$repo_dir/main/mvs_device_profile.c" \
  "$repo_dir/main/mvs_usb_profile.c" \
  -lm -o "$out_dir/generic_acp_host_tests"
"$out_dir/generic_acp_host_tests"
"$repo_dir/tests/host/compare_a800x_baseline.sh"
