#!/bin/sh

# Exit if any command fails
set -e

# Compile the program
(
  cd "$(dirname "$0")"
  cmake -B build -S .
  cmake --build build
)

# Run the compiled server
exec ./build/server "$@"
