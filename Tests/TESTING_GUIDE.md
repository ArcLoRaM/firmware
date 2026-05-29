# ArcLoRaM Unit Testing Guide

A complete reference for writing, building, and running host-side unit tests on the ArcLoRaM firmware.

---

## 1. What this is

### Host-only tests — not firmware tests

The firmware runs on a **STM32WL55** chip (Cortex-M4 + Cortex-M0+). Unit tests run on **your Windows PC** using a normal GCC compiler. The two builds are completely separate:

| | Firmware build | Unit test build |
|---|---|---|
| **Tool** | STM32CubeIDE (Eclipse) | CMake + MinGW GCC |
| **Compiler** | `arm-none-eabi-gcc` | `gcc` (MSYS2 UCRT64) |
| **Runs on** | STM32 chip | Your PC |
| **Config file** | `.ioc` / `.cproject` | `Tests/CMakePresets.json` |

You never need to flash anything to run tests. Results appear in your terminal in under a second.

### Unity Test Framework

[Unity](https://github.com/ThrowTheSwitch/Unity) is a minimal C unit test framework designed for embedded systems. A test file looks like this:

```c
#include "unity.h"
#include "my_module.h"

void setUp(void)    {}   // runs before each test
void tearDown(void) {}   // runs after  each test

void test_something(void)
{
    uint8_t result = my_function(5);
    TEST_ASSERT_EQUAL(10, result);  // expected=10, actual=result
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_something);
    return UNITY_END();
}
```

Unity is downloaded automatically by CMake the first time you configure — no manual installation needed.

### What code belongs in tests

The testable folders are:

```
CM4/SubGHz_Phy/Logic/       ← CM4 business logic
CM0PLUS/SubGHz_Phy/Logic/   ← CM0+ business logic
Common/SharedMemory/        ← shared memory layout helpers
Common/Protocol/            ← frame encoding/decoding, state machines
```

Code in these folders should be **pure logic**: it takes inputs, computes outputs, and has no direct hardware register access. Hardware-touching code (HAL calls, DMA, IRQ handlers) belongs outside Logic/ and is handled by the stub layer described in Section 2.

---

## 2. How the stub layer works

### The problem

Your Logic `.c` files will include headers like `stm32wlxx_hal.h`. The real HAL header chain eventually includes CMSIS `core_cm4.h`, which contains ARM-specific assembly intrinsics. A Windows GCC cannot compile those — it would fail with dozens of errors about unknown types and instructions.

### The solution: header shadowing

The `Tests/stubs/` directory contains simplified replacements for every HAL header that Logic code might include. CMake places `Tests/stubs/` **first** in the include search path. When the compiler processes:

```c
#include "stm32wlxx_hal.h"
```

it finds the stub version in `Tests/stubs/stm32wlxx_hal.h` before it ever looks at the real HAL. The stub provides the types and function signatures the Logic code needs, implemented as plain-C no-ops.

### Provided stubs

| File | What it stubs |
|---|---|
| `stm32wlxx_hal_def.h` | `HAL_StatusTypeDef`, `HAL_OK`, `__IO`, `__weak` |
| `stm32wlxx_hal.h` | `HAL_GetTick()`, `HAL_Delay()` |
| `stm32wlxx_hal_conf.h` | intentionally empty (prevents module includes) |
| `stm32wlxx_hal_rtc.h` | `RTC_HandleTypeDef`, `RTC_TimeTypeDef`, `HAL_RTC_GetTime()`, … |
| `stm32wlxx_hal_gpio.h` | `GPIO_PinState`, `HAL_GPIO_ReadPin()`, `HAL_GPIO_WritePin()` |
| `core_cm4.h` | `__disable_irq()`, `__enable_irq()`, `__DSB()`, `__NOP()`, … (all no-ops) |
| `stm32wl55xx.h` | empty (no peripheral register maps on host) |
| `stm32wlxx.h` | includes the two stubs above |

### Adding a new stub

When you add Logic code that includes a HAL header not yet stubbed, the build will fail with:

```
fatal error: stm32wlxx_hal_uart.h: No such file or directory
```

Fix: create `Tests/stubs/stm32wlxx_hal_uart.h` with the minimum content needed. Copy the pattern from an existing stub — only add the types and functions your Logic code actually uses. Do not copy the entire real HAL header.

### The `HOST_TEST` escape hatch

If a Logic file has a small section that genuinely cannot run on a PC (e.g., a function that reads a hardware register directly), wrap it:

```c
#ifndef HOST_TEST
    /* real hardware code — compiles only when building for the STM32 */
    my_register = PERIPHERAL->REG;
#else
    /* host-safe stub — compiles only when building tests on a PC */
    my_register = 0U;
#endif
```

The `HOST_TEST` macro is defined automatically by `Tests/stubs/CMakeLists.txt` for all test builds. Use this sparingly — if you find yourself using it often, it is a sign that the function needs to be split into a hardware part and a logic part.

---

## 3. Setup checklist (one-time)

### Toolchain (already confirmed present)

Open a PowerShell terminal and verify:

```powershell
gcc --version        # should print: gcc (Rev..., Built by MSYS2...)
cmake --version      # should print: cmake version 3.x or 4.x
mingw32-make --version   # should print: GNU Make 4.x.x
```

If any of these fail:
- **gcc / mingw32-make missing**: Install MSYS2 from https://www.msys2.org/, then in the MSYS2 UCRT64 terminal run `pacman -S mingw-w64-ucrt-x86_64-gcc`. Add `C:\msys64\ucrt64\bin` to your Windows PATH.
- **cmake missing**: Same MSYS2 terminal: `pacman -S mingw-w64-ucrt-x86_64-cmake`. Or download from https://cmake.org/download/ and tick "Add to PATH" during install.

### VSCode extensions

Install these two from the Extensions panel (`Ctrl+Shift+X`):

- **CMake Tools** — publisher: Microsoft (`ms-vscode.cmake-tools`)
- **C/C++** — publisher: Microsoft (`ms-vscode.cpptools`)

After installing CMake Tools, reload VSCode. The bottom status bar should show CMake kit and build/test buttons.

### Internet access for first configure

The first time you run `cmake --preset host-mingw`, CMake downloads Unity from GitHub (~500 KB). This happens once and is cached in `Tests/build/_deps/`. After that, configuring works offline.

---

## 4. First run / verification

Open a PowerShell terminal at the **workspace root** and run:

```powershell
# Step 1 — configure (downloads Unity on first run)
cd Tests
cmake --preset host-mingw
```

Expected output (first run):
```
-- The C compiler identification is GNU 14.2.0
...
-- Configuring done
-- Build files have been written to: ...\Tests\build
```

```powershell
# Step 2 — compile everything
cmake --build --preset host-mingw
```

Expected output:
```
[ 25%] Building C object ...
[ 50%] Building C object unit/CMakeFiles/test_placeholder.dir/test_placeholder.c.obj
[100%] Linking C executable test_placeholder.exe
[100%] Built target test_placeholder
```

```powershell
# Step 3 — run all tests
ctest --preset host-mingw --verbose
```

Expected output:
```
1/1 Test #1: test_placeholder ................... Passed    0.01 sec

100% tests passed, 0 tests failed out of 1
```

You can also run a single test executable directly to see each test name:

```powershell
.\build\unit\test_placeholder.exe
```

Expected:
```
test_placeholder.c:32:test_placeholder_always_passes:PASS

-----------------------
1 Tests 0 Failures 0 Ignored
OK
```

### Running from VSCode

After CMake Tools is configured, the bottom status bar shows:
- **Build** (hammer icon) — equivalent to `cmake --build --preset host-mingw`
- **Run CTest** (flask icon) — equivalent to `ctest --preset host-mingw`

Click **Build** first, then **Run CTest**.

---

## 5. Adding a new test

Assume you have just written `Common/Protocol/frame_encoder.c` and `frame_encoder.h`.

### Step 1 — Create the test file

Create `Tests/unit/test_frame_encoder.c`:

```c
#include "unity.h"
#include "frame_encoder.h"   /* your module's own header */

void setUp(void)    {}
void tearDown(void) {}

void test_encode_uplink_produces_correct_length(void)
{
    uint8_t buf[64];
    uint8_t payload[] = { 0xAA, 0xBB };

    int len = frame_encode_uplink(buf, sizeof(buf), payload, sizeof(payload));

    /* TEST_ASSERT_EQUAL(expected, actual) */
    TEST_ASSERT_EQUAL(6, len);   /* header(4) + payload(2) */
}

void test_encode_rejects_oversized_payload(void)
{
    uint8_t buf[64];
    uint8_t big_payload[256] = {0};

    int len = frame_encode_uplink(buf, sizeof(buf), big_payload, sizeof(big_payload));

    /* Negative return = error */
    TEST_ASSERT_TRUE(len < 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_uplink_produces_correct_length);
    RUN_TEST(test_encode_rejects_oversized_payload);
    return UNITY_END();
}
```

**Notes:**
- One test file per module is a good starting rule.
- Each `void test_xxx(void)` function is one test case.
- `setUp()` / `tearDown()` are mandatory but can be empty.
- The `main()` block is mandatory — copy it, add `RUN_TEST()` for each new function.

### Step 2 — Register the test in CMakeLists.txt

Open [Tests/unit/CMakeLists.txt](unit/CMakeLists.txt) and add:

```cmake
add_unity_test(test_frame_encoder
    test_frame_encoder.c
    ../../Common/Protocol/frame_encoder.c
)
```

The path `../../Common/Protocol/frame_encoder.c` is relative to `Tests/unit/`. If your module has a dependency (e.g., `frame_encoder.c` calls functions in `crc.c`), add that `.c` file to the same line:

```cmake
add_unity_test(test_frame_encoder
    test_frame_encoder.c
    ../../Common/Protocol/frame_encoder.c
    ../../Common/Protocol/crc.c       # dependency
)
```

### Step 3 — Handle stub errors

Rebuild. If you see a compile error like:

```
fatal error: stm32wlxx_hal_lptim.h: No such file or directory
```

Create `Tests/stubs/stm32wlxx_hal_lptim.h` with the minimum content:

```c
#ifndef STM32WLxx_HAL_LPTIM_H
#define STM32WLxx_HAL_LPTIM_H
#include "stm32wlxx_hal_def.h"

/* Add only what frame_encoder.c actually uses from this header */
typedef struct { uint32_t Instance; } LPTIM_HandleTypeDef;

static inline HAL_StatusTypeDef HAL_LPTIM_Counter_Start(LPTIM_HandleTypeDef *h, uint32_t Period)
    { (void)h; (void)Period; return HAL_OK; }

#endif
```

No changes to `stubs/CMakeLists.txt` are needed — it exposes the entire `Tests/stubs/` directory, so any header you add there is immediately available.

### Step 4 — Build and run

```powershell
cmake --preset host-mingw        # only needed when CMakeLists.txt changed
cmake --build --preset host-mingw
ctest --preset host-mingw --verbose
```

Or from VSCode: **Build** → **Run CTest**.

---

## 6. Unity assertion cheat-sheet

| Macro | Checks |
|---|---|
| `TEST_ASSERT_EQUAL(exp, act)` | `exp == act` (integers) |
| `TEST_ASSERT_EQUAL_HEX8(exp, act)` | same, prints in hex (good for byte masks) |
| `TEST_ASSERT_EQUAL_HEX16(exp, act)` | same, 16-bit |
| `TEST_ASSERT_EQUAL_HEX32(exp, act)` | same, 32-bit |
| `TEST_ASSERT_EQUAL_FLOAT(exp, act)` | floating-point with tolerance |
| `TEST_ASSERT_EQUAL_STRING(exp, act)` | null-terminated string comparison |
| `TEST_ASSERT_EQUAL_MEMORY(exp, act, len)` | byte-by-byte memory comparison |
| `TEST_ASSERT_TRUE(cond)` | `cond` is non-zero |
| `TEST_ASSERT_FALSE(cond)` | `cond` is zero |
| `TEST_ASSERT_NULL(ptr)` | pointer is NULL |
| `TEST_ASSERT_NOT_NULL(ptr)` | pointer is not NULL |
| `TEST_ASSERT_GREATER_THAN(thresh, act)` | `act > thresh` |
| `TEST_ASSERT_LESS_OR_EQUAL(thresh, act)` | `act <= thresh` |
| `TEST_FAIL_MESSAGE("reason")` | unconditional failure with a message |
| `TEST_IGNORE_MESSAGE("reason")` | marks test as ignored (skipped), with a message |

**Convention:** always put `expected` first, `actual` second. Unity's error message will then read "Expected X Was Y", which is much easier to read than "Expected Y Was X".

---

## 7. Troubleshooting

### `fatal error: <header>.h: No such file or directory`

A Logic file includes a HAL header that does not have a stub yet. Create `Tests/stubs/<header>.h` following the pattern in Section 2. You only need to add the types and functions that the compiler error trace references.

### `cmake: command not found` or `gcc: command not found`

The MSYS2 `bin` directory is not in your PATH. Add `C:\msys64\ucrt64\bin` to your Windows PATH (System Properties → Environment Variables → Path → New) and open a new terminal.

### CMake picks the wrong compiler (arm-none-eabi-gcc)

The preset hard-codes `"CMAKE_C_COMPILER": "gcc"`. If CMake still picks the wrong one, call it explicitly:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER="C:/msys64/ucrt64/bin/gcc.exe"
```

### `mingw32-make: *** No rule to make target`

Run `cmake --preset host-mingw` (configure step) before `cmake --build`. The Makefile must be regenerated after any `CMakeLists.txt` change.

### A test crashes (segfault / access violation) instead of failing

The Logic code being tested dereferences a hardware register address (e.g., `RTC->TR`). This is a memory address that does not exist on your PC. You have two options:
1. Wrap the register access with `#ifndef HOST_TEST ... #endif` and provide a testable alternative.
2. Inject the dependency through a function pointer or callback so the test can substitute a no-op.

### `ctest` shows 0 tests

`enable_testing()` must be called before `add_subdirectory()` in `CMakeLists.txt`. It already is — if you see 0 tests, check that the `add_unity_test()` calls are not commented out in `unit/CMakeLists.txt`.

### Unity assertion fails with confusing output

Double-check the argument order. Unity always expects `(expected, actual)`. Swapping them makes the error message backwards.
