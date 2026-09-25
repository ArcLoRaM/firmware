/* Tests/stubs/arclog_capture.h - host-test capture of ArcLog lines.
 *
 * Every ARCLOG() call made by code under test is formatted exactly as on
 * target (minus the RTC timestamp) and appended to a ring buffer. Tests
 * reset it in setUp() and assert on the events that were emitted:
 *
 *     ArcLog_CaptureReset();
 *     MAC_OnSyncPacketReceived(&pkt, 1000u);
 *     TEST_ASSERT_ARCLOG("CLK from=COLD to=ACQ");
 */
#ifndef ARCLOG_CAPTURE_H
#define ARCLOG_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#define ARCLOG_CAPTURE_MAX_LINES  64u
#define ARCLOG_CAPTURE_LINE_LEN   256u

void        ArcLog_CaptureReset(void);
uint32_t    ArcLog_CaptureCount(void);
const char *ArcLog_CaptureLine(uint32_t idx);

/*! Index of the first captured line containing \p needle, or -1. */
int32_t     ArcLog_CaptureFind(const char *needle);

/*! Assert that some captured line contains the substring. */
#define TEST_ASSERT_ARCLOG(needle) \
    TEST_ASSERT_TRUE_MESSAGE(ArcLog_CaptureFind(needle) >= 0, "missing ArcLog: " needle)

/*! Assert that no captured line contains the substring. */
#define TEST_ASSERT_NO_ARCLOG(needle) \
    TEST_ASSERT_TRUE_MESSAGE(ArcLog_CaptureFind(needle) < 0, "unexpected ArcLog: " needle)

#endif /* ARCLOG_CAPTURE_H */
