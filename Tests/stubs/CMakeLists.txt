# INTERFACE library: no .c source files of its own.
# It bundles the Tests/stubs/ directory as an include path and adds compile
# definitions. Every test executable that links hal_stubs automatically gets
# these settings without repeating them in each test's CMakeLists.txt entry.
add_library(hal_stubs INTERFACE)

target_include_directories(hal_stubs INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# HOST_TEST  — check this in Logic code to guard hardware-only sections:
#                 #ifndef HOST_TEST
#                   /* real hardware code — runs on STM32 only */
#                 #endif
# STM32WL55xx / CORE_CM4 / USE_HAL_DRIVER — replicate the firmware defines
# so that any #if defined(...) guards in Logic headers resolve correctly.
target_compile_definitions(hal_stubs INTERFACE
    HOST_TEST
    STM32WL55xx
    CORE_CM4
    USE_HAL_DRIVER
)
