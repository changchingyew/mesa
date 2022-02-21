#!/bin/bash

SCRIPT_DIR="$( cd "$(dirname "${BASH_SOURCE[0]:-$0}")" >/dev/null 2>&1 ; pwd -P )"
. "${SCRIPT_DIR}/env.sh"

set -x

meson build -Dgallium-drivers=d3d12,swrast -Dplatforms=x11 -Dgallium-va=enabled

cd subprojects/DirectX-Headers-1.0
patch -p1 < ../../Fix-directx-headers-build.patch
cd ../..

ninja -C build
