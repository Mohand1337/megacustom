#!/bin/bash
# Create FULLY portable bundle with ALL libraries

set -e

PROJECT_DIR="$(pwd)"
BUILD_DIR="$PROJECT_DIR/build-qt"
BUNDLE_DIR="$PROJECT_DIR/MegaCustomGUI-full-portable"

echo "Creating fully portable bundle..."

rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR/lib"
mkdir -p "$BUNDLE_DIR/plugins/platforms"
mkdir -p "$BUNDLE_DIR/plugins/xcbglintegrations"
mkdir -p "$BUNDLE_DIR/plugins/imageformats"
mkdir -p "$BUNDLE_DIR/plugins/tls"
mkdir -p "$BUNDLE_DIR/resources"

# Copy executable
cp "$BUILD_DIR/MegaCustomGUI" "$BUNDLE_DIR/"

# Copy resources
cp -r "$PROJECT_DIR/resources/styles" "$BUNDLE_DIR/resources/" 2>/dev/null || true
cp -r "$PROJECT_DIR/resources/icons" "$BUNDLE_DIR/resources/" 2>/dev/null || true

# Copy ALL linked libraries (recursive)
copy_deps() {
    local binary="$1"
    ldd "$binary" 2>/dev/null | grep "=> /" | awk '{print $3}' | while read lib; do
        if [ -f "$lib" ] && [ ! -f "$BUNDLE_DIR/lib/$(basename "$lib")" ]; then
            cp "$lib" "$BUNDLE_DIR/lib/"
            echo "  $(basename "$lib")"
            # Recursively copy dependencies of this library
            copy_deps "$lib"
        fi
    done
}

echo "Copying all libraries..."
copy_deps "$BUILD_DIR/MegaCustomGUI"

# Copy Qt plugins
QT_PLUGIN_PATH="/usr/lib/x86_64-linux-gnu/qt6/plugins"
if [ -d "$QT_PLUGIN_PATH" ]; then
    echo "Copying Qt plugins..."
    cp "$QT_PLUGIN_PATH/platforms/libqxcb.so" "$BUNDLE_DIR/plugins/platforms/" 2>/dev/null || true
    cp "$QT_PLUGIN_PATH/platforms/libqoffscreen.so" "$BUNDLE_DIR/plugins/platforms/" 2>/dev/null || true
    cp "$QT_PLUGIN_PATH/platforms/libqlinuxfb.so" "$BUNDLE_DIR/plugins/platforms/" 2>/dev/null || true
    cp "$QT_PLUGIN_PATH/xcbglintegrations/"*.so "$BUNDLE_DIR/plugins/xcbglintegrations/" 2>/dev/null || true
    cp "$QT_PLUGIN_PATH/imageformats/"*.so "$BUNDLE_DIR/plugins/imageformats/" 2>/dev/null || true
    cp "$QT_PLUGIN_PATH/tls/"*.so "$BUNDLE_DIR/plugins/tls/" 2>/dev/null || true
    
    # Copy plugin dependencies too
    for plugin in "$BUNDLE_DIR/plugins"/*/*.so; do
        [ -f "$plugin" ] && copy_deps "$plugin"
    done
fi

# Create launcher script
cat > "$BUNDLE_DIR/run.sh" << 'EOF'
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
EOF
chmod +x "$BUNDLE_DIR/run.sh"

# Create a zip for easy transfer
echo "Creating zip archive..."
cd "$PROJECT_DIR"
rm -f MegaCustomGUI-portable.zip
zip -r MegaCustomGUI-portable.zip MegaCustomGUI-full-portable

SIZE=$(du -sh "$BUNDLE_DIR" | cut -f1)
ZIP_SIZE=$(du -sh MegaCustomGUI-portable.zip | cut -f1)

echo ""
echo "=========================================="
echo "Portable bundle: $BUNDLE_DIR ($SIZE)"
echo "ZIP archive: MegaCustomGUI-portable.zip ($ZIP_SIZE)"
echo ""
echo "To use on VPS:"
echo "  1. Copy MegaCustomGUI-portable.zip to your VPS"
echo "  2. unzip MegaCustomGUI-portable.zip"
echo "  3. cd MegaCustomGUI-full-portable"
echo "  4. ./run.sh"
echo "=========================================="
