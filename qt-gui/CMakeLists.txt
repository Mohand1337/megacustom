cmake_minimum_required(VERSION 3.16)
project(MegaCustomGUI VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Define that Mega SDK is available since we're linking to it
add_definitions(-DMEGA_SDK_AVAILABLE)

# ENABLE_SYNC must match how the SDK was built - it changes function signatures
# Only enable on Windows where the SDK is built with ENABLE_SYNC via vcpkg
if(WIN32)
    add_definitions(-DENABLE_SYNC)
endif()

# =============================================================================
# Platform-Specific Configuration
# =============================================================================

if(WIN32)
    message(STATUS "Configuring for Windows")

    # Hide console window for GUI app
    set(CMAKE_WIN32_EXECUTABLE ON)

    # Windows version targeting (Windows 10+)
    add_compile_definitions(
        NTDDI_VERSION=0x0A000000
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )

    # Use static VC++ runtime for better portability (optional)
    # set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # Silence some MSVC warnings
    if(MSVC)
        add_compile_options(/W3 /wd4996 /wd4251)
    endif()
else()
    message(STATUS "Configuring for Unix/Linux")
endif()

# Find required packages
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Concurrent Sql Svg)
qt_standard_project_setup()

# Optional: QtKeychain for secure credential storage
# If not found, CredentialStore falls back to encrypted file storage
find_package(Qt6Keychain QUIET)
if(Qt6Keychain_FOUND)
    add_definitions(-DHAVE_QTKEYCHAIN)
    message(STATUS "QtKeychain found - using OS secure storage")
else()
    message(STATUS "QtKeychain not found - using encrypted file fallback")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main
    ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/models
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scheduler
    ${CMAKE_CURRENT_SOURCE_DIR}/src/styles
    ${CMAKE_CURRENT_SOURCE_DIR}/src/search
    ${CMAKE_CURRENT_SOURCE_DIR}/src/accounts
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
    ${CMAKE_CURRENT_BINARY_DIR}/stubs
    # Include paths for CLI modules
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sdk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sdk/bindings/qt
)

# Collect only existing source files
set(SOURCES
    src/main.cpp
    src/main/Application.cpp
    src/main/MainWindow.cpp
    src/main/MainWindowSlots.cpp
    src/widgets/FileExplorer.cpp
    src/widgets/FileExplorerSlots.cpp
    src/widgets/TransferQueue.cpp
    src/widgets/FolderMapperPanel.cpp
    src/widgets/MultiUploaderPanel.cpp
    src/widgets/SmartSyncPanel.cpp
    src/widgets/CloudCopierPanel.cpp
    src/widgets/MegaSidebar.cpp
    src/widgets/TopToolbar.cpp
    src/widgets/BreadcrumbWidget.cpp
    src/widgets/SettingsPanel.cpp
    src/widgets/LoadingSpinner.cpp
    src/widgets/MemberRegistryPanel.cpp
    src/widgets/DistributionPanel.cpp
    src/widgets/DownloaderPanel.cpp
    src/widgets/WatermarkPanel.cpp
    src/widgets/ElidedLabel.cpp
    src/widgets/BannerWidget.cpp
    src/widgets/CircularProgressBar.cpp
    src/widgets/StatusIndicator.cpp
    src/widgets/SwitchButton.cpp
    src/widgets/LogViewerPanel.cpp
    src/utils/MemberRegistry.cpp
    src/utils/IconProvider.cpp
    src/utils/TemplateExpander.cpp
    src/dialogs/LoginDialog.cpp
    src/dialogs/AboutDialog.cpp
    src/dialogs/MappingEditDialog.cpp
    src/dialogs/DistributionRuleDialog.cpp
    src/dialogs/SyncProfileDialog.cpp
    src/dialogs/AddDestinationDialog.cpp
    src/dialogs/ConflictResolutionDialog.cpp
    src/dialogs/ScheduleSyncDialog.cpp
    src/dialogs/CopyConflictDialog.cpp
    src/dialogs/RemoteFolderBrowserDialog.cpp
    src/dialogs/BulkPathEditorDialog.cpp
    src/dialogs/BulkNameEditorDialog.cpp
    src/dialogs/AccountManagerDialog.cpp
    src/dialogs/WordPressConfigDialog.cpp
    src/dialogs/WordPressSyncPreviewDialog.cpp
    src/dialogs/WatermarkSettingsDialog.cpp
    src/controllers/AuthController.cpp
    src/controllers/TransferProgressListener.cpp
    src/controllers/TransferController.cpp
    src/controllers/FileController.cpp
    src/controllers/FolderMapperController.cpp
    src/controllers/MultiUploaderController.cpp
    src/controllers/SmartSyncController.cpp
    src/controllers/CloudCopierController.cpp
    src/controllers/DistributionController.cpp
    src/controllers/WatermarkerController.cpp
    src/scheduler/SyncScheduler.cpp
    src/utils/Settings.cpp
    src/styles/MegaProxyStyle.cpp
    src/styles/ThemeManager.cpp
    # Search module for Everything-like instant search
    src/search/CloudSearchIndex.cpp
    src/search/SearchQueryParser.cpp
    src/widgets/SearchResultsPanel.cpp
    src/widgets/AdvancedSearchPanel.cpp
    src/widgets/AccountSwitcherWidget.cpp
    # Multi-account support
    src/accounts/CredentialStore.cpp
    src/accounts/SessionPool.cpp
    src/accounts/AccountManager.cpp
    src/accounts/TransferLogStore.cpp
    src/accounts/CrossAccountTransferManager.cpp
    src/widgets/CrossAccountLogPanel.cpp
    src/widgets/QuickPeekPanel.cpp
    # Modern widget components (Phase 2)
    src/widgets/SvgIcon.cpp
    src/widgets/IconButton.cpp
    src/widgets/ModernMenu.cpp
    src/widgets/ButtonFactory.cpp
    # Dialog management (Phase 3)
    src/dialogs/DialogManager.cpp
    # Stylesheet generation (Phase 4)
    src/styles/StyleSheetGenerator.cpp
    # CLI module source files
    ../src/core/MegaManager.cpp
    ../src/core/AuthenticationModule.cpp
    ../src/core/ConfigManager.cpp
    ../src/core/Crypto.cpp
    ../src/core/PathValidator.cpp
    ../src/operations/FileOperations.cpp
    ../src/operations/FolderManager.cpp
    ../src/features/FolderMapper.cpp
    ../src/features/CloudCopier.cpp
    ../src/features/Watermarker.cpp
    ../src/core/LogManager.cpp
    ../src/integrations/MemberDatabase.cpp
    ../src/integrations/WordPressSync.cpp
    ../src/features/DistributionPipeline.cpp
)

set(HEADERS
    src/main/Application.h
    src/main/MainWindow.h
    src/widgets/FileExplorer.h
    src/widgets/TransferQueue.h
    src/widgets/FolderMapperPanel.h
    src/widgets/MultiUploaderPanel.h
    src/widgets/SmartSyncPanel.h
    src/widgets/CloudCopierPanel.h
    src/widgets/MegaSidebar.h
    src/widgets/TopToolbar.h
    src/widgets/BreadcrumbWidget.h
    src/widgets/SettingsPanel.h
    src/widgets/LoadingSpinner.h
    src/widgets/MemberRegistryPanel.h
    src/widgets/DistributionPanel.h
    src/widgets/DownloaderPanel.h
    src/widgets/WatermarkPanel.h
    src/widgets/ElidedLabel.h
    src/widgets/BannerWidget.h
    src/widgets/CircularProgressBar.h
    src/widgets/StatusIndicator.h
    src/widgets/SwitchButton.h
    src/widgets/LogViewerPanel.h
    src/utils/MemberRegistry.h
    src/utils/IconProvider.h
    src/utils/TemplateExpander.h
    src/dialogs/LoginDialog.h
    src/dialogs/AboutDialog.h
    src/dialogs/MappingEditDialog.h
    src/dialogs/DistributionRuleDialog.h
    src/dialogs/SyncProfileDialog.h
    src/dialogs/AddDestinationDialog.h
    src/dialogs/ConflictResolutionDialog.h
    src/dialogs/ScheduleSyncDialog.h
    src/dialogs/CopyConflictDialog.h
    src/dialogs/RemoteFolderBrowserDialog.h
    src/dialogs/BulkPathEditorDialog.h
    src/dialogs/BulkNameEditorDialog.h
    src/dialogs/AccountManagerDialog.h
    src/dialogs/WordPressConfigDialog.h
    src/dialogs/WordPressSyncPreviewDialog.h
    src/dialogs/WatermarkSettingsDialog.h
    src/controllers/AuthController.h
    src/controllers/TransferProgressListener.h
    src/controllers/FileController.h
    src/controllers/TransferController.h
    src/controllers/FolderMapperController.h
    src/controllers/MultiUploaderController.h
    src/controllers/SmartSyncController.h
    src/controllers/CloudCopierController.h
    src/controllers/DistributionController.h
    src/controllers/WatermarkerController.h
    src/scheduler/SyncScheduler.h
    src/utils/Settings.h
    src/styles/MegaProxyStyle.h
    # Search module headers
    src/search/CloudSearchIndex.h
    src/search/SearchQueryParser.h
    src/widgets/SearchResultsPanel.h
    src/widgets/AdvancedSearchPanel.h
    src/widgets/AccountSwitcherWidget.h
    # Multi-account support headers
    src/accounts/AccountModels.h
    src/accounts/CredentialStore.h
    src/accounts/SessionPool.h
    src/accounts/AccountManager.h
    src/accounts/TransferLogStore.h
    src/accounts/CrossAccountTransferManager.h
    src/widgets/CrossAccountLogPanel.h
    src/widgets/QuickPeekPanel.h
    # Modern design system headers (Phase 1 & 2)
    src/styles/DesignTokens.h
    src/styles/ThemeManager.h
    src/widgets/SvgIcon.h
    src/widgets/IconButton.h
    src/widgets/ModernMenu.h
    src/widgets/ButtonFactory.h
    # Dialog management (Phase 3)
    src/dialogs/DialogManager.h
    # Stylesheet generation (Phase 4)
    src/styles/StyleSheetGenerator.h
)

# Qt Resources
set(RESOURCES
    resources/icons.qrc
)

# Windows resource file (icon, version info)
if(WIN32)
    set(WIN_RC_FILE resources/app.rc)
endif()

# Create executable
qt_add_executable(MegaCustomGUI
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
    ${WIN_RC_FILE}
)

# =============================================================================
# MEGA SDK Library Configuration
# =============================================================================

if(WIN32)
    # Windows: Use VCPKG-installed libraries
    find_package(OpenSSL REQUIRED)
    find_package(CURL REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(unofficial-sodium CONFIG REQUIRED)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
    find_package(cryptopp CONFIG REQUIRED)
    find_package(ICU COMPONENTS uc i18n data REQUIRED)
    find_package(c-ares CONFIG REQUIRED)
    find_package(libuv CONFIG REQUIRED)

    # SDK library paths for Windows
    set(MEGA_SDK_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sdk/build_sdk/Release/SDKlib.lib)
    set(CCRONEXPR_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sdk/build_sdk/third_party/ccronexpr/Release/ccronexpr.lib)

    # Windows resource file for icon and version info
    set(WIN_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc)
else()
    # Linux: Use system libraries
    set(MEGA_SDK_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sdk/build_sdk/libSDKlib.a)
    set(CCRONEXPR_LIB ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sdk/build_sdk/third_party/ccronexpr/libccronexpr.a)
endif()

# Link Qt libraries and Mega SDK
target_link_libraries(MegaCustomGUI PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::Sql
    Qt6::Svg
    ${MEGA_SDK_LIB}
    ${CCRONEXPR_LIB}
)

# Platform-specific library linking
if(WIN32)
    target_link_libraries(MegaCustomGUI PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        CURL::libcurl
        ZLIB::ZLIB
        unofficial-sodium::sodium
        unofficial::sqlite3::sqlite3
        cryptopp::cryptopp
        ICU::uc
        ICU::i18n
        ICU::data
        c-ares::cares
        $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
        ws2_32      # Windows Sockets
        crypt32     # Windows Crypto API
        winhttp     # Windows HTTP
        iphlpapi    # IP Helper API
        shlwapi     # Shell API (PathIsRelativeW)
    )
else()
    target_link_libraries(MegaCustomGUI PRIVATE
        # System libraries needed by Mega SDK (Linux)
        pthread
        z
        crypto
        ssl
        curl
        sodium
        sqlite3
        icuuc
        icui18n
        icudata
        crypto++
        cares
        dl
        rt
    )
endif()

# Set properties
set_target_properties(MegaCustomGUI PROPERTIES
    AUTOMOC ON
)

# Installation
install(TARGETS MegaCustomGUI
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# mega_ops CLI Tool
qt_add_executable(mega_ops
    tools/mega_ops.cpp
)

target_link_libraries(mega_ops PRIVATE
    Qt6::Core
    Qt6::Network
    ${MEGA_SDK_LIB}
    ${CCRONEXPR_LIB}
)

# Platform-specific linking for mega_ops
if(WIN32)
    target_link_libraries(mega_ops PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        CURL::libcurl
        ZLIB::ZLIB
        unofficial-sodium::sodium
        unofficial::sqlite3::sqlite3
        cryptopp::cryptopp
        ICU::uc
        ICU::i18n
        ICU::data
        c-ares::cares
        $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
        ws2_32
        crypt32
        winhttp
        iphlpapi
        shlwapi
    )
else()
    target_link_libraries(mega_ops PRIVATE
        pthread
        z
        crypto
        ssl
        curl
        sodium
        sqlite3
        icuuc
        icui18n
        icudata
        crypto++
        cares
        dl
        rt
    )
endif()

set_target_properties(mega_ops PROPERTIES
    AUTOMOC ON
)