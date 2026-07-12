add_executable(watermarker_integration_tests
    WatermarkerIntegrationTests.cpp
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
