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

add_executable(persistence_integration_tests
    PersistenceIntegrationTests.cpp
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
