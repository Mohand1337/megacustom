#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Set library path to use bundled libs first
export LD_LIBRARY_PATH="$SCRIPT_DIR/lib:$LD_LIBRARY_PATH"

# Set Qt paths
export QT_PLUGIN_PATH="$SCRIPT_DIR/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$SCRIPT_DIR/plugins/platforms"

# Resource path
export MEGACUSTOM_RESOURCE_PATH="$SCRIPT_DIR/resources"

# Run
exec "$SCRIPT_DIR/MegaCustomGUI" "$@"
