#!/bin/bash

BASEDIR="$(dirname "${BASH_SOURCE[0]}")"

cd "$BASEDIR" || exit 1

mkdir build
cd build || exit 1
cmake -DENABLE_TINT2CONF=1 .. || exit 1
cp src/tint2conf/tint2conf .
cp compile_commands.json .. || exit 1
make || exit 1
