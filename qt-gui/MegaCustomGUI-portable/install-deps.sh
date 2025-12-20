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
