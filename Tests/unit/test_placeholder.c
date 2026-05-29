/* Tests/unit/test_placeholder.c
 *
 * This file's only purpose is to confirm that the test infrastructure
 * compiles and runs correctly.  Delete it once you have a real test. */

#include "unity.h"   /* provided by Unity after FetchContent downloads it */


/* setUp() is called by Unity automatically before every test function.
 * Use it to reset state or initialize variables shared across tests.
 * Leave empty if there is nothing to set up. */
void setUp(void) {}

/* tearDown() is called after every test function.
 * Use it to free resources or clean up. Leave empty if unneeded. */
void tearDown(void) {}


void test_placeholder_always_passes(void)
{
    /* TEST_ASSERT_EQUAL(expected, actual)
     * Fails the test if the two values are not equal and prints:
     *   Expected 42 Was <actual>
     * Change to a real assertion once this file becomes a real test. */
    TEST_ASSERT_EQUAL(42, 42);
}


/* Every Unity test file needs its own main().
 * UNITY_BEGIN() initialises the framework.
 * RUN_TEST() runs one test function and records pass/fail.
 * UNITY_END() prints the summary and returns the failure count.
 * CTest reads the exit code: 0 = all passed, non-zero = at least one failed. */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_placeholder_always_passes);
    return UNITY_END();
}
