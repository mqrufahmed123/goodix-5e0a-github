#!/usr/bin/env bash
# Local software tests; never open the sensor or read enrolled prints.
set -euo pipefail
task_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
task_tmp_dir=$(mktemp -d)
trap 'rm -rf -- "$task_tmp_dir"' EXIT
read -r -a task_glib_cflags <<< "$(pkg-config --cflags glib-2.0)"
read -r -a task_glib_libs <<< "$(pkg-config --libs glib-2.0)"
task_cc_flags=(-O2 -g)
if [[ ${GOODIX_SANITIZERS:-0} == 1 ]]; then
    task_cc_flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
for task_name in test-calibration test-normalization; do
    "${CC:-cc}" "${task_cc_flags[@]}" "${task_glib_cflags[@]}" \
        -I"$task_root/libfprint-driver" -I"$task_root/tests/native" \
        "$task_root/tests/native/$task_name.c" -o "$task_tmp_dir/$task_name" \
        "${task_glib_libs[@]}" -lm
    "$task_tmp_dir/$task_name"
done
