# =============================================================================
# add_unity_test(TEST_NAME  src1.c  [src2.c ...])
#
# Creates a test executable and registers it with CTest.
#
#   TEST_NAME  — name used for the executable and for "ctest -R <name>"
#   src files  — the test file PLUS every .c file you want to test
#
# The paths to source files are relative to THIS directory (Tests/unit/).
# Use ../../ to reach the project root.  Examples:
#
#   add_unity_test(test_cell_eligibility
#       test_cell_eligibility.c
#       ../../Common/Protocol/cell_eligibility.c
#   )
#
#   add_unity_test(test_cm4_scheduler
#       test_cm4_scheduler.c
#       ../../CM4/SubGHz_Phy/Logic/scheduler.c
#   )
# =============================================================================
macro(add_unity_test TEST_NAME)
    # Create an executable from all sources listed after TEST_NAME
    add_executable(${TEST_NAME} ${ARGN})

    # Link the Unity framework and the HAL stubs
    target_link_libraries(${TEST_NAME} PRIVATE unity hal_stubs)

    # Expose the Logic source directories so #include "my_module.h" resolves.
    # CMAKE_SOURCE_DIR is Tests/, so ../ goes to ArcLoRaM_Base/.
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/../CM4/SubGHz_Phy/Logic
        ${CMAKE_SOURCE_DIR}/../CM0PLUS/SubGHz_Phy/Logic
        ${CMAKE_SOURCE_DIR}/../Common/SharedMemory
        ${CMAKE_SOURCE_DIR}/../Common/Protocol
    )

    # Register with CTest — running "ctest" will execute this binary
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endmacro()

# =============================================================================
# Tests
# =============================================================================

# Placeholder — verifies the toolchain works. Delete once real tests exist.
add_unity_test(test_placeholder
    test_placeholder.c
)

# -----------------------------------------------------------------------------
# ADD NEW TESTS HERE using the macro above:
#
# add_unity_test(test_my_module
#     test_my_module.c
#     ../../Common/Protocol/my_module.c
# )
# -----------------------------------------------------------------------------

add_unity_test(test_tdma_table
    test_tdma_table.c
    ../../Common/Protocol/tdma_table.c
)

add_unity_test(test_protocol_types
    test_protocol_types.c
)

add_unity_test(test_freq_resolver
    test_freq_resolver.c
    ../../CM0PLUS/SubGHz_Phy/Logic/freq_resolver.c
)

add_unity_test(test_compliance_engine
    test_compliance_engine.c
    ../../CM0PLUS/SubGHz_Phy/Logic/compliance_engine.c
)

add_unity_test(test_mac_state_machine_c1
    test_mac_state_machine_c1.c
    ../../CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c1.c
    ../../Common/Protocol/tdma_table.c
)

add_unity_test(test_mac_state_machine_c2
    test_mac_state_machine_c2.c
    ../../CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c2.c
    ../../Common/Protocol/tdma_table.c
)

add_unity_test(test_mac_state_machine_c3
    test_mac_state_machine_c3.c
    ../../CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c3.c
    ../../Common/Protocol/tdma_table.c
)

add_unity_test(test_tdma_machine_c2
    test_tdma_machine_c2.c
    ../../CM0PLUS/SubGHz_Phy/Logic/tdma_machine.c
    ../../CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c2.c
    ../../CM0PLUS/SubGHz_Phy/Logic/freq_resolver.c
    ../../CM0PLUS/SubGHz_Phy/Logic/compliance_engine.c
    ../../Common/Protocol/tdma_table.c
)
target_compile_definitions(test_tdma_machine_c2 PRIVATE NODE_CLASS=2)

add_unity_test(test_tdma_machine_c1
    test_tdma_machine_c1.c
    ../../CM0PLUS/SubGHz_Phy/Logic/tdma_machine.c
    ../../CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c1.c
    ../../CM0PLUS/SubGHz_Phy/Logic/freq_resolver.c
    ../../CM0PLUS/SubGHz_Phy/Logic/compliance_engine.c
    ../../Common/Protocol/tdma_table.c
)
target_compile_definitions(test_tdma_machine_c1 PRIVATE NODE_CLASS=1)

add_unity_test(test_tdma_machine_c2_2sync
    test_tdma_machine_c2_2sync.c
    stub_tdma_table_2sync.c
    ../../CM0PLUS/SubGHz_Phy/Logic/tdma_machine.c
    ../../CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c2.c
    ../../CM0PLUS/SubGHz_Phy/Logic/freq_resolver.c
    ../../CM0PLUS/SubGHz_Phy/Logic/compliance_engine.c
)
target_compile_definitions(test_tdma_machine_c2_2sync PRIVATE NODE_CLASS=2)

add_unity_test(test_cm4_diag
    test_cm4_diag.c
    ../../CM4/SubGHz_Phy/Logic/cm4_diag.c
)

add_unity_test(test_rtc_ssr
    test_rtc_ssr.c
)
