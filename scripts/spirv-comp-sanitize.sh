#!/usr/bin/bash

if [[ $# < 2 ]]; then
  echo "Usage: $0 <ifname> <ofname>"

  exit 1
fi

cd "$(dirname "$0")"/..

BASE_FILEPATH="$(mktemp)"
LL_FILEPATH="$BASE_FILEPATH.ll"
LL_MOD_FILEPATH="$BASE_FILEPATH-mod.ll"
BC_FILEPATH="$BASE_FILEPATH-mod.bc"

clang -target spirv64 -S -emit-llvm -o "$LL_FILEPATH" "$1"
opt --load-pass-plugin=plugin/build/libSPIRVComputeSanitizer.so --passes="function(spirv-compute-sanitizer)" -S -o "$LL_MOD_FILEPATH" "$LL_FILEPATH"
llvm-as -o "$BC_FILEPATH" "$LL_MOD_FILEPATH"
llvm-spirv -o "$2" "$BC_FILEPATH"

rm -f "$LL_FILEPATH" "$LL_MOD_FILEPATH" "$BC_FILEPATH"

