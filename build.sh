#!/bin/bash

set -x

export CC=/usr/bin/clang
export CXX=/usr/bin/clang++

meson build -Dgallium-drivers=d3d12,swrast -Dplatforms=x11 -Dgallium-va=enabled

cd subprojects/DirectX-Headers-1.0
patch -p1 < ../../Fix-directx-headers-build.patch
cd ../..

ninja -C build
