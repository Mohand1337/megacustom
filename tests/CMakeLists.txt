add_executable(watermarker_integration_tests
    WatermarkerIntegrationTests.cpp
)

set_target_properties(watermarker_integration_tests PROPERTIES
    WIN32_EXECUTABLE OFF
)

target_link_libraries(watermarker_integration_tests
    PRIVATE
        mega_custom_core
)

add_test(
    NAME watermarker_integration
    COMMAND watermarker_integration_tests
)

set_tests_properties(watermarker_integration PROPERTIES TIMEOUT 90)

add_executable(persistence_integration_tests
    PersistenceIntegrationTests.cpp
)

set_target_properties(persistence_integration_tests PROPERTIES
    WIN32_EXECUTABLE OFF
)

target_link_libraries(persistence_integration_tests
    PRIVATE
        mega_custom_core
)

add_test(
    NAME persistence_integration
    COMMAND persistence_integration_tests
)

set_tests_properties(persistence_integration PROPERTIES TIMEOUT 30)

if(TARGET Qt6::Core)
    add_executable(watermark_diagnostics_tests
        WatermarkDiagnosticsTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../qt-gui/src/utils/WatermarkDiagnostics.cpp
    )

    set_target_properties(watermark_diagnostics_tests PROPERTIES
        WIN32_EXECUTABLE OFF
    )

    target_include_directories(watermark_diagnostics_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../qt-gui/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../qt-gui/src/utils
    )

    target_link_libraries(watermark_diagnostics_tests PRIVATE Qt6::Core)

    add_test(
        NAME watermark_diagnostics
        COMMAND watermark_diagnostics_tests
    )

    set_tests_properties(watermark_diagnostics PROPERTIES TIMEOUT 30)

    add_executable(member_registry_integration_tests
        MemberRegistryIntegrationTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../qt-gui/src/utils/MemberRegistry.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../qt-gui/src/utils/Settings.cpp
    )

    set_target_properties(member_registry_integration_tests PROPERTIES
        AUTOMOC ON
        WIN32_EXECUTABLE OFF
    )

    target_include_directories(member_registry_integration_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../qt-gui/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../qt-gui/src/utils
    )

    target_link_libraries(member_registry_integration_tests PRIVATE Qt6::Core)

    add_test(
        NAME member_registry_integration
        COMMAND member_registry_integration_tests
    )

    set_tests_properties(member_registry_integration PROPERTIES TIMEOUT 30)
endif()
