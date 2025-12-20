#!/bin/bash

#############################################
# MegaCustom App Build Script
# Checks dependencies and builds the project
#############################################

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_NAME="MegaCustom"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
MEGA_SDK_DIR="${PROJECT_DIR}/third_party/sdk"
VCPKG_DIR="${PROJECT_DIR}/third_party/vcpkg"

# Build options
BUILD_TYPE="${BUILD_TYPE:-Release}"
USE_CMAKE="${USE_CMAKE:-auto}"
INSTALL_DEPS="${INSTALL_DEPS:-false}"
VERBOSE="${VERBOSE:-false}"

# Function to print colored messages
print_msg() {
    local color=$1
    local msg=$2
    echo -e "${color}${msg}${NC}"
}

print_header() {
    echo ""
    print_msg "$BLUE" "================================================="
    print_msg "$BLUE" " $PROJECT_NAME Build Script"
    print_msg "$BLUE" "================================================="
    echo ""
}

# Check if a command exists
check_command() {
    local cmd=$1
    local name=$2
    local install_msg=$3

    if command -v $cmd &> /dev/null; then
        print_msg "$GREEN" "✓ $name found: $(command -v $cmd)"
        return 0
    else
        print_msg "$RED" "✗ $name not found"
        if [ ! -z "$install_msg" ]; then
            print_msg "$YELLOW" "  Install: $install_msg"
        fi
        return 1
    fi
}

# Check C++ compiler
check_compiler() {
    print_msg "$BLUE" "Checking C++ compiler..."

    local compiler_found=false
    if check_command "g++" "g++" "sudo apt-get install build-essential (Ubuntu/Debian)"; then
        compiler_found=true
        CXX_COMPILER="g++"
    elif check_command "clang++" "clang++" "sudo apt-get install clang (Ubuntu/Debian)"; then
        compiler_found=true
        CXX_COMPILER="clang++"
    fi

    if [ "$compiler_found" = true ]; then
        # Check C++ standard support
        local test_file="/tmp/test_cpp17.cpp"
        echo "#include <optional>" > $test_file
        echo "int main() { std::optional<int> x; return 0; }" >> $test_file

        if $CXX_COMPILER -std=c++17 -c $test_file -o /tmp/test_cpp17.o 2>/dev/null; then
            print_msg "$GREEN" "  C++17 support: OK"
            rm -f /tmp/test_cpp17.o $test_file
            return 0
        else
            print_msg "$RED" "  C++17 support: FAILED"
            rm -f /tmp/test_cpp17.o $test_file
            return 1
        fi
    fi

    return 1
}

# Check build tools
check_build_tools() {
    print_msg "$BLUE" "\nChecking build tools..."

    local cmake_found=false
    local make_found=false

    if check_command "cmake" "CMake" "sudo apt-get install cmake (Ubuntu/Debian)"; then
        cmake_found=true
        CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
        print_msg "$GREEN" "  Version: $CMAKE_VERSION"
    fi

    if check_command "make" "Make" "sudo apt-get install build-essential (Ubuntu/Debian)"; then
        make_found=true
    fi

    # Determine which build system to use
    if [ "$USE_CMAKE" = "auto" ]; then
        if [ "$cmake_found" = true ]; then
            USE_CMAKE="yes"
        elif [ "$make_found" = true ]; then
            USE_CMAKE="no"
        else
            print_msg "$RED" "No build system found!"
            return 1
        fi
    fi

    return 0
}

# Check required libraries
check_libraries() {
    print_msg "$BLUE" "\nChecking required libraries..."

    local all_found=true

    # Check OpenSSL
    if pkg-config --exists openssl 2>/dev/null; then
        print_msg "$GREEN" "✓ OpenSSL found"
    else
        print_msg "$YELLOW" "⚠ OpenSSL not found (may still work if in system paths)"
        print_msg "$YELLOW" "  Install: sudo apt-get install libssl-dev (Ubuntu/Debian)"
    fi

    # Check CURL
    if pkg-config --exists libcurl 2>/dev/null; then
        print_msg "$GREEN" "✓ CURL found"
    else
        print_msg "$YELLOW" "⚠ CURL not found (may still work if in system paths)"
        print_msg "$YELLOW" "  Install: sudo apt-get install libcurl4-openssl-dev (Ubuntu/Debian)"
    fi

    # Check ZLIB
    if pkg-config --exists zlib 2>/dev/null; then
        print_msg "$GREEN" "✓ ZLIB found"
    else
        print_msg "$YELLOW" "⚠ ZLIB not found (may still work if in system paths)"
        print_msg "$YELLOW" "  Install: sudo apt-get install zlib1g-dev (Ubuntu/Debian)"
    fi

    return 0
}

# Check optional libraries
check_optional_libraries() {
    print_msg "$BLUE" "\nChecking optional libraries..."

    # Check PCRE2
    if pkg-config --exists libpcre2-8 2>/dev/null; then
        print_msg "$GREEN" "✓ PCRE2 found (regex features enabled)"
        HAVE_PCRE2=true
    else
        print_msg "$YELLOW" "⚠ PCRE2 not found (regex features disabled)"
        print_msg "$YELLOW" "  Install: sudo apt-get install libpcre2-dev (Ubuntu/Debian)"
        HAVE_PCRE2=false
    fi

    # Check nlohmann_json
    if [ -f "/usr/include/nlohmann/json.hpp" ] || [ -f "/usr/local/include/nlohmann/json.hpp" ]; then
        print_msg "$GREEN" "✓ nlohmann_json found"
        HAVE_NLOHMANN_JSON=true
    else
        print_msg "$YELLOW" "⚠ nlohmann_json not found (using simple JSON)"
        print_msg "$YELLOW" "  Install: sudo apt-get install nlohmann-json3-dev (Ubuntu/Debian)"
        HAVE_NLOHMANN_JSON=false
    fi

    return 0
}

# Check Mega SDK
check_mega_sdk() {
    print_msg "$BLUE" "\nChecking Mega SDK..."

    if [ -d "$MEGA_SDK_DIR" ] && [ -f "$MEGA_SDK_DIR/include/megaapi.h" ]; then
        print_msg "$GREEN" "✓ Mega SDK found at: $MEGA_SDK_DIR"
        return 0
    else
        print_msg "$YELLOW" "⚠ Mega SDK not found"
        print_msg "$YELLOW" "  To install:"
        print_msg "$YELLOW" "  cd $PROJECT_DIR"
        print_msg "$YELLOW" "  git clone https://github.com/meganz/sdk.git third_party/sdk"
        return 1
    fi
}

# Build with CMake
build_with_cmake() {
    print_msg "$BLUE" "\nBuilding with CMake..."

    # Create build directory
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    # Configure
    print_msg "$GREEN" "Configuring..."
    local cmake_args="-DCMAKE_BUILD_TYPE=$BUILD_TYPE"

    if [ "$HAVE_PCRE2" = false ]; then
        cmake_args="$cmake_args -DUSE_PCRE2=OFF"
    fi

    if [ "$HAVE_NLOHMANN_JSON" = true ]; then
        cmake_args="$cmake_args -DUSE_NLOHMANN_JSON=ON"
    fi

    if [ "$VERBOSE" = true ]; then
        cmake $cmake_args ..
    else
        cmake $cmake_args .. > /dev/null 2>&1
    fi

    # Build
    print_msg "$GREEN" "Building..."
    if [ "$VERBOSE" = true ]; then
        cmake --build . --config $BUILD_TYPE
    else
        cmake --build . --config $BUILD_TYPE > /dev/null 2>&1
    fi

    print_msg "$GREEN" "✓ Build complete: $BUILD_DIR/megacustom"
}

# Build with Make
build_with_make() {
    print_msg "$BLUE" "\nBuilding with Make..."

    cd "$PROJECT_DIR"

    # Set debug flag
    local debug_flag=1
    if [ "$BUILD_TYPE" = "Release" ]; then
        debug_flag=0
    fi

    # Clean if needed
    if [ -f "megacustom" ]; then
        print_msg "$GREEN" "Cleaning previous build..."
        make clean > /dev/null 2>&1
    fi

    # Build
    print_msg "$GREEN" "Building..."
    if [ "$VERBOSE" = true ]; then
        make DEBUG=$debug_flag
    else
        make DEBUG=$debug_flag > /dev/null 2>&1
    fi

    if [ -f "megacustom" ]; then
        print_msg "$GREEN" "✓ Build complete: $PROJECT_DIR/megacustom"
        return 0
    else
        print_msg "$RED" "✗ Build failed"
        return 1
    fi
}

# Install dependencies
install_dependencies() {
    print_msg "$BLUE" "\nInstalling dependencies..."

    # Detect OS
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        OS=$ID
    else
        print_msg "$RED" "Cannot detect OS"
        return 1
    fi

    case $OS in
        ubuntu|debian)
            print_msg "$GREEN" "Installing packages for Ubuntu/Debian..."
            sudo apt-get update
            sudo apt-get install -y \
                build-essential \
                cmake \
                libssl-dev \
                libcurl4-openssl-dev \
                zlib1g-dev \
                libpcre2-dev \
                nlohmann-json3-dev \
                git
            ;;
        fedora|rhel|centos)
            print_msg "$GREEN" "Installing packages for Fedora/RHEL/CentOS..."
            sudo dnf install -y \
                gcc-c++ \
                cmake \
                openssl-devel \
                libcurl-devel \
                zlib-devel \
                pcre2-devel \
                json-devel \
                git
            ;;
        arch|manjaro)
            print_msg "$GREEN" "Installing packages for Arch/Manjaro..."
            sudo pacman -S --needed \
                base-devel \
                cmake \
                openssl \
                curl \
                zlib \
                pcre2 \
                nlohmann-json \
                git
            ;;
        *)
            print_msg "$RED" "Unsupported OS: $OS"
            return 1
            ;;
    esac

    print_msg "$GREEN" "✓ Dependencies installed"
}

# Main function
main() {
    print_header

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --debug)
                BUILD_TYPE="Debug"
                shift
                ;;
            --release)
                BUILD_TYPE="Release"
                shift
                ;;
            --cmake)
                USE_CMAKE="yes"
                shift
                ;;
            --make)
                USE_CMAKE="no"
                shift
                ;;
            --install-deps)
                INSTALL_DEPS="true"
                shift
                ;;
            --verbose|-v)
                VERBOSE="true"
                shift
                ;;
            --help|-h)
                echo "Usage: $0 [OPTIONS]"
                echo ""
                echo "Options:"
                echo "  --debug          Build in Debug mode"
                echo "  --release        Build in Release mode (default)"
                echo "  --cmake          Force use of CMake"
                echo "  --make           Force use of Make"
                echo "  --install-deps   Install system dependencies"
                echo "  --verbose, -v    Verbose output"
                echo "  --help, -h       Show this help"
                exit 0
                ;;
            *)
                print_msg "$RED" "Unknown option: $1"
                exit 1
                ;;
        esac
    done

    print_msg "$GREEN" "Build configuration:"
    print_msg "$GREEN" "  Project directory: $PROJECT_DIR"
    print_msg "$GREEN" "  Build type: $BUILD_TYPE"

    # Install dependencies if requested
    if [ "$INSTALL_DEPS" = true ]; then
        install_dependencies
    fi

    # Check dependencies
    local checks_passed=true

    if ! check_compiler; then
        checks_passed=false
    fi

    if ! check_build_tools; then
        checks_passed=false
    fi

    check_libraries
    check_optional_libraries
    check_mega_sdk

    if [ "$checks_passed" = false ]; then
        print_msg "$RED" "\n✗ Some required dependencies are missing"
        print_msg "$YELLOW" "  Run with --install-deps to install them"
        exit 1
    fi

    # Build the project
    print_msg "$BLUE" "\n================================================="
    print_msg "$BLUE" " Building $PROJECT_NAME"
    print_msg "$BLUE" "================================================="

    if [ "$USE_CMAKE" = "yes" ]; then
        build_with_cmake
    else
        build_with_make
    fi

    local build_result=$?

    if [ $build_result -eq 0 ]; then
        print_msg "$GREEN" "\n================================================="
        print_msg "$GREEN" " Build Successful!"
        print_msg "$GREEN" "================================================="
        print_msg "$GREEN" "\nTo run the application:"
        if [ "$USE_CMAKE" = "yes" ]; then
            print_msg "$GREEN" "  $BUILD_DIR/megacustom help"
        else
            print_msg "$GREEN" "  $PROJECT_DIR/megacustom help"
        fi
    else
        print_msg "$RED" "\n================================================="
        print_msg "$RED" " Build Failed"
        print_msg "$RED" "================================================="
        print_msg "$YELLOW" "\nTry running with --verbose for more details"
        exit 1
    fi
}

# Run main function
main "$@"