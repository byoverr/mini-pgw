#!/bin/bash

set -e

BUILD_DIR="build"

if [ ! -d "$BUILD_DIR" ]; then
    echo "You need to build the project: ./build.sh"
    exit 1
fi

cd "$BUILD_DIR"

echo "Запуск тестов..."
ctest --output-on-failure

echo "All tests passed!"

