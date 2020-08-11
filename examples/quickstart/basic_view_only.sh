#!/usr/bin/env bash
path_to_script=$(realpath $(dirname $0))
path_to_root=${path_to_script}/../..
path_to_examples=${path_to_root}/examples
path_to_basic=${path_to_examples}/basic_miniapp_run

hpcviewer ${path_to_basic}/example_run/miniapp.exe.hpcdatabase &
hpctraceviewer ${path_to_basic}/example_run/miniapp.exe.hpcdatabase &
wait
