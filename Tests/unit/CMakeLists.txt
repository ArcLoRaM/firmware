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

add_unity_test(test_day_ms
    test_day_ms.c
)

# The Sync schedule against the 1 % duty cycle: the bring-up table (denied after
# ~7 min) and the Build Overrides of the multi-hour runs (never denied).
add_unity_test(test_sync_budget_default
    test_sync_budget.c
    ../../Common/Protocol/tdma_table.c
    ../../CM0PLUS/SubGHz_Phy/Logic/compliance_engine.c
)
add_unity_test(test_sync_budget_run
    test_sync_budget.c
    ../../Common/Protocol/tdma_table.c
    ../../CM0PLUS/SubGHz_Phy/Logic/compliance_engine.c
)
target_compile_definitions(test_sync_budget_run PRIVATE
    SYNC_TX_BUDGET=1u SYNC_CELL_GAP_MS=9500u EXPECT_WITHIN_BUDGET)

# The MAC takes the SyncStamp in ticks and judges the error in us (C1 and C2 MAC).
foreach(cls c1 c2)
    add_unity_test(test_mac_fine_stamp_${cls}
        test_mac_fine_stamp.c
        ../../CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_${cls}.c
        ../../Common/Protocol/tdma_table.c
    )
endforeach()

# Timing probe helper: with BENCH_PROBE (lines, wrap, limit) and production (nothing).
add_unity_test(test_probe
    test_probe.c
    ../../Common/Log/probe.c
)
target_compile_definitions(test_probe PRIVATE BENCH_PROBE)
add_unity_test(test_probe_off
    test_probe.c
    ../../Common/Log/probe.c
)

add_unity_test(test_sync_stamp
    test_sync_stamp.c
)

add_unity_test(test_lora_toa
    test_lora_toa.c
)

add_unity_test(test_rtc_set_plan
    test_rtc_set_plan.c
)

add_unity_test(test_rtc_calr
    test_rtc_calr.c
)

add_unity_test(test_calr_policy
    test_calr_policy.c
)

add_unity_test(test_drift_estimator
    test_drift_estimator.c
    ../../CM0PLUS/SubGHz_Phy/Logic/drift_estimator.c
)
target_link_libraries(test_drift_estimator PRIVATE m)

add_unity_test(test_date_bcd
    test_date_bcd.c
)

add_unity_test(test_protocol_types
    test_protocol_types.c
)

add_unity_test(test_freq_resolver
    test_freq_resolver.c
    ../../CM0PLUS/SubGHz_Phy/Logic/freq_resolver.c
    ../../Common/SharedMemory/shared_mem.c
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
    ../../CM0PLUS/SubGHz_Phy/Logic/guard_time_resolver.c
    ../../Common/Protocol/tdma_table.c
    ../../Common/SharedMemory/shared_mem.c
)
target_compile_definitions(test_tdma_machine_c2 PRIVATE NODE_CLASS=2)

add_unity_test(test_tdma_machine_c1
    test_tdma_machine_c1.c
    ../../CM0PLUS/SubGHz_Phy/Logic/tdma_machine.c
    ../../CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c1.c
    ../../CM0PLUS/SubGHz_Phy/Logic/freq_resolver.c
    ../../CM0PLUS/SubGHz_Phy/Logic/compliance_engine.c
    ../../CM0PLUS/SubGHz_Phy/Logic/guard_time_resolver.c
    ../../Common/Protocol/tdma_table.c
    ../../Common/SharedMemory/shared_mem.c
)
target_compile_definitions(test_tdma_machine_c1 PRIVATE NODE_CLASS=1)

add_unity_test(test_tdma_machine_c2_2sync
    test_tdma_machine_c2_2sync.c
    stub_tdma_table_2sync.c
    ../../CM0PLUS/SubGHz_Phy/Logic/tdma_machine.c
    ../../CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c2.c
    ../../CM0PLUS/SubGHz_Phy/Logic/freq_resolver.c
    ../../CM0PLUS/SubGHz_Phy/Logic/compliance_engine.c
    ../../CM0PLUS/SubGHz_Phy/Logic/guard_time_resolver.c
    ../../Common/SharedMemory/shared_mem.c
)
target_compile_definitions(test_tdma_machine_c2_2sync PRIVATE NODE_CLASS=2)

add_unity_test(test_tdma_machine_c3
    test_tdma_machine_c3.c
    ../../CM0PLUS/SubGHz_Phy/Logic/tdma_machine.c
    ../../CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c3.c
    ../../CM0PLUS/SubGHz_Phy/Logic/freq_resolver.c
    ../../CM0PLUS/SubGHz_Phy/Logic/compliance_engine.c
    ../../CM0PLUS/SubGHz_Phy/Logic/guard_time_resolver.c
    ../../Common/Protocol/tdma_table.c
    ../../Common/SharedMemory/shared_mem.c
)
target_compile_definitions(test_tdma_machine_c3 PRIVATE NODE_CLASS=3)

add_unity_test(test_tdma_machine_c3_beacon
    test_tdma_machine_c3_beacon.c
    stub_tdma_table_beacon.c
    ../../CM0PLUS/SubGHz_Phy/Logic/tdma_machine.c
    ../../CM0PLUS/SubGHz_Phy/Logic/mac_state_machine_c3.c
    ../../CM0PLUS/SubGHz_Phy/Logic/freq_resolver.c
    ../../CM0PLUS/SubGHz_Phy/Logic/compliance_engine.c
    ../../CM0PLUS/SubGHz_Phy/Logic/guard_time_resolver.c
    ../../Common/SharedMemory/shared_mem.c
)
target_compile_definitions(test_tdma_machine_c3_beacon PRIVATE NODE_CLASS=3)

add_unity_test(test_cm4_diag
    test_cm4_diag.c
    ../../CM4/SubGHz_Phy/Logic/cm4_diag.c
)

add_unity_test(test_rtc_ssr
    test_rtc_ssr.c
)

add_unity_test(test_node_id
    test_node_id.c
    ../../Common/Protocol/node_id.c
)
