# Simple Makefile for MegaCustom App
# Use this when CMake is not available

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I./include -I./third_party/sdk/include -DMEGA_SDK_AVAILABLE
# SDK is now built! Linking with real Mega SDK library
LDFLAGS = -pthread -L./third_party/sdk/build_sdk -L./third_party/sdk/build_sdk/third_party/ccronexpr -lSDKlib -lccronexpr -lcrypto++ -lsodium -lz -lcurl -lssl -lcrypto -licuuc -licudata -lsqlite3 -lreadline

# Check for PCRE2 availability
PCRE2_EXISTS := $(shell pkg-config --exists libpcre2-8 2>/dev/null && echo yes)
ifeq ($(PCRE2_EXISTS), yes)
    CXXFLAGS += $(shell pkg-config --cflags libpcre2-8)
    LDFLAGS += $(shell pkg-config --libs libpcre2-8)
endif

# Debug vs Release
DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g -O0 -DDEBUG
else
    CXXFLAGS += -O3 -DNDEBUG
endif

# Output binary
TARGET = megacustom

# Source files
SRCS = src/main.cpp \
       src/core/ConfigManager.cpp \
       src/core/MegaManager.cpp \
       src/core/AuthenticationModule.cpp \
       src/core/Crypto.cpp \
       src/core/Error.cpp \
       src/core/PathValidator.cpp \
       src/core/TransferListenerBase.cpp \
       src/core/LogManager.cpp \
       src/operations/FileOperations.cpp \
       src/operations/FolderManager.cpp \
       src/features/RegexRenamer.cpp \
       src/features/MultiUploader.cpp \
       src/features/SmartSync.cpp \
       src/features/FolderMapper.cpp \
       src/features/Watermarker.cpp \
       src/features/DistributionPipeline.cpp \
       src/integrations/MemberDatabase.cpp \
       src/integrations/WordPressSync.cpp \
       src/cli/CommandRegistry.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)

# Build rules
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "Clean complete"

# Run the program
run: $(TARGET)
	./$(TARGET) help

# Build and run integrated test
test: test_integrated.cpp src/core/ConfigManager.o src/core/MegaManager.o src/core/AuthenticationModule.o src/operations/FileOperations.o src/operations/FolderManager.o src/features/RegexRenamer.o src/features/MultiUploader.o src/features/SmartSync.o src/features/FolderMapper.o src/features/Watermarker.o
	$(CXX) $(CXXFLAGS) test_integrated.cpp src/core/ConfigManager.o src/core/MegaManager.o src/core/AuthenticationModule.o src/operations/FileOperations.o src/operations/FolderManager.o src/features/RegexRenamer.o src/features/MultiUploader.o src/features/SmartSync.o src/features/FolderMapper.o src/features/Watermarker.o -o test_integrated $(LDFLAGS)
	./test_integrated

# Show help
help:
	@echo "Available targets:"
	@echo "  all     - Build the application (default)"
	@echo "  clean   - Remove build files"
	@echo "  run     - Build and run with help command"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "Build options:"
	@echo "  DEBUG=0 - Build release version"
	@echo "  DEBUG=1 - Build debug version (default)"

.PHONY: all clean run help