#!/usr/bin/env bash
set -e

PLATFORM=${PLATFORM:-PI5_DEBIAN_64BIT}
DEBUG=${DEBUG:-1}

echo "Building buptest for ${PLATFORM} (DEBUG=${DEBUG})..."
make buptest PLATFORM="${PLATFORM}" DEBUG="${DEBUG}"
