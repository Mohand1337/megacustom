#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Set library path
export LD_LIBRARY_PATH="$SCRIPT_DIR/lib:$LD_LIBRARY_PATH"

# Set Qt plugin path
export QT_PLUGIN_PATH="$SCRIPT_DIR/plugins"

# Set resource path (for stylesheet)
export MEGACUSTOM_RESOURCE_PATH="$SCRIPT_DIR/resources"

# Run the application
exec "$SCRIPT_DIR/MegaCustomGUI" "$@"
