#!/bin/bash

set -e

BUILD_DIR="build"

echo "Start building the project..."

if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake ..
cmake --build . -j$(nproc)

echo "Build complete!"

