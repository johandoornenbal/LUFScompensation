#!/usr/bin/env bash
# Source this file in the prepared Debian cloud instance.
export PATH="/workspace/lufs-tools/bin:$PATH"
export PYTHONPATH="/workspace/lufs-tools${PYTHONPATH:+:$PYTHONPATH}"
export CPLUS_INCLUDE_PATH="/workspace/sysroot/usr/include:/workspace/sysroot/usr/include/freetype2${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
export C_INCLUDE_PATH="/workspace/sysroot/usr/include${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
export LIBRARY_PATH="/workspace/sysroot/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"
export PKG_CONFIG_SYSROOT_DIR=/workspace/sysroot
export PKG_CONFIG_LIBDIR=/workspace/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:/workspace/sysroot/usr/share/pkgconfig

# Fontconfig needs a writable cache when rendering the editor headlessly.
export XDG_CACHE_HOME=/workspace/lufs-cache
mkdir -p "$XDG_CACHE_HOME"
