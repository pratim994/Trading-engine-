#!/bin/bash
# Kept for compatibility: build.sh is now incremental by default. Builds release + debug like the old script.
exec "$(dirname "$0")/build.sh" --all "$@"
