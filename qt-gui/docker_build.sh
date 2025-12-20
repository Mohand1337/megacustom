#!/bin/bash

# Docker-based Qt6 Build Script for MegaCustom
# This script builds the Qt6 application inside a Docker container

set -e

echo "==========================================="
echo "Docker-based Qt6 Build for MegaCustom"
echo "==========================================="
echo

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo -e "${RED}✗ Docker is not installed${NC}"
    echo "Please install Docker first:"
    echo "  https://docs.docker.com/get-docker/"
    exit 1
fi

echo -e "${GREEN}✓ Docker found${NC}"

# Check if Docker Compose is available
if command -v docker-compose &> /dev/null; then
    COMPOSE_CMD="docker-compose"
elif docker compose version &> /dev/null 2>&1; then
    COMPOSE_CMD="docker compose"
else
    echo -e "${RED}✗ Docker Compose not found${NC}"
    echo "Please install Docker Compose or update Docker"
    exit 1
fi

echo -e "${GREEN}✓ Docker Compose found${NC}"

# Build options
BUILD_MODE=${1:-build}

case "$BUILD_MODE" in
    build)
        echo
        echo -e "${YELLOW}Building Docker image with Qt6...${NC}"
        $COMPOSE_CMD build qt-builder

        echo
        echo -e "${YELLOW}Building Qt application in container...${NC}"
        $COMPOSE_CMD run --rm qt-builder

        echo
        echo -e "${GREEN}✓ Build complete!${NC}"
        echo "Build output is in: build-qt/"
        ;;

    shell)
        echo
        echo -e "${YELLOW}Starting interactive shell in container...${NC}"
        $COMPOSE_CMD run --rm qt-builder /bin/bash
        ;;

    clean)
        echo
        echo -e "${YELLOW}Cleaning build artifacts...${NC}"
        $COMPOSE_CMD down -v
        rm -rf build-qt/
        echo -e "${GREEN}✓ Clean complete${NC}"
        ;;

    test)
        echo
        echo -e "${YELLOW}Running tests in container...${NC}"
        $COMPOSE_CMD run --rm qt-builder /bin/bash -c "cd /app && ./test_build_structure.sh"
        ;;

    *)
        echo "Usage: $0 [build|shell|clean|test]"
        echo "  build - Build the Qt application (default)"
        echo "  shell - Start an interactive shell in the container"
        echo "  clean - Remove build artifacts and containers"
        echo "  test  - Run structure tests"
        exit 1
        ;;
esac