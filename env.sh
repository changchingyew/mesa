#!/bin/bash

#set -x
DIR=$(dirname $(readlink -f "${BASH_SOURCE[0]}" 2>/dev/null||echo $0))

export LIBVA_DRIVERS_PATH=$PWD/build/src/gallium/targets/va/
export LIBVA_DRIVER_NAME=libgallium
export LIBVA_TRACE=$PWD/libva.log

export CC=/usr/bin/clang
export CXX=/usr/bin/clang++
