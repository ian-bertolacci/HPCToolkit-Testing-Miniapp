#!/usr/bin/env bash
path_to_script=$(realpath $(dirname $0))
path_to_root=${path_to_script}/../..
path_to_miniapp=${path_to_root}/miniapp
path_to_examples=${path_to_root}/examples
path_to_basic=${path_to_examples}/basic_miniapp_run

cd ${path_to_miniapp}
make build

cd ${path_to_basic}
./run.sh

hpcviewer miniapp.exe.hpcdatabase &
hpctraceviewer miniapp.exe.hpcdatabase &
wait
