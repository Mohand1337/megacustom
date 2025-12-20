#!/bin/bash
# Create a portable bundle of MegaCustomGUI

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build-qt"
BUNDLE_DIR="$PROJECT_DIR/MegaCustomGUI-portable"

echo "Creating portable bundle..."

# Clean and create bundle directory
rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR/lib"
mkdir -p "$BUNDLE_DIR/plugins/platforms"
mkdir -p "$BUNDLE_DIR/plugins/xcbglintegrations"
mkdir -p "$BUNDLE_DIR/resources"

# Copy executable
cp "$BUILD_DIR/MegaCustomGUI" "$BUNDLE_DIR/"

# Copy resources
cp -r "$PROJECT_DIR/resources/styles" "$BUNDLE_DIR/resources/" 2>/dev/null || true
cp -r "$PROJECT_DIR/resources/icons" "$BUNDLE_DIR/resources/" 2>/dev/null || true

# Copy Qt plugins (essential for Qt to work)
QT_PLUGIN_PATH="/usr/lib/x86_64-linux-gnu/qt6/plugins"
if [ -d "$QT_PLUGIN_PATH" ]; then
    cp "$QT_PLUGIN_PATH/platforms/libqxcb.so" "$BUNDLE_DIR/plugins/platforms/" 2>/dev/null || true
    cp "$QT_PLUGIN_PATH/platforms/libqoffscreen.so" "$BUNDLE_DIR/plugins/platforms/" 2>/dev/null || true
    cp "$QT_PLUGIN_PATH/xcbglintegrations/"*.so "$BUNDLE_DIR/plugins/xcbglintegrations/" 2>/dev/null || true
fi

# Copy required libraries
copy_lib() {
    local lib_path=$(ldd "$BUILD_DIR/MegaCustomGUI" | grep "$1" | awk '{print $3}')
    if [ -n "$lib_path" ] && [ -f "$lib_path" ]; then
        cp "$lib_path" "$BUNDLE_DIR/lib/"
        echo "  Copied: $1"
    fi
}

echo "Copying Qt libraries..."
copy_lib "libQt6Widgets"
copy_lib "libQt6Gui"
copy_lib "libQt6Core"
copy_lib "libQt6DBus"
copy_lib "libQt6XcbQpa"

echo "Copying other libraries..."
copy_lib "libcrypto.so.3"
copy_lib "libssl.so.3"
copy_lib "libcurl.so.4"
copy_lib "libsodium"
copy_lib "libsqlite3"
copy_lib "libcrypto++"
copy_lib "libicuuc"
copy_lib "libicudata"

# Create launcher script
cat > "$BUNDLE_DIR/run.sh" << 'EOF'
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
EOF
chmod +x "$BUNDLE_DIR/run.sh"

# Create install-deps script for VPS
cat > "$BUNDLE_DIR/install-deps.sh" << 'EOF'
#!/bin/bash
# Install minimal runtime dependencies on Ubuntu/Debian VPS

echo "Installing runtime dependencies..."
sudo apt update
sudo apt install -y \
    libqt6widgets6 libqt6gui6 libqt6core6 libqt6dbus6 \
    libssl3 libcurl4 libsodium23 libsqlite3-0 \
    libcrypto++8 libicu74 \
    libxcb1 libx11-6 libxkbcommon0 libfontconfig1 \
    libfreetype6 libpng16-16 \
    xauth

echo ""
echo "Done! You can now run: ./run.sh"
echo ""
echo "For X11 forwarding, connect with: ssh -X user@server"
EOF
chmod +x "$BUNDLE_DIR/install-deps.sh"

# Calculate size
BUNDLE_SIZE=$(du -sh "$BUNDLE_DIR" | cut -f1)

echo ""
echo "=========================================="
echo "Portable bundle created: $BUNDLE_DIR"
echo "Size: $BUNDLE_SIZE"
echo ""
echo "To deploy to VPS:"
echo "  1. Copy the folder: scp -r $BUNDLE_DIR user@vps:~/"
echo "  2. On VPS, run: ./install-deps.sh"
echo "  3. Connect with X11: ssh -X user@vps"
echo "  4. Run: ./run.sh"
echo "=========================================="
