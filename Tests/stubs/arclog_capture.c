/* Tests/stubs/arclog_capture.c - host-test ArcLog backend (see arclog_capture.h). */
#include "arclog_capture.h"
#include "arclog.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char     s_lines[ARCLOG_CAPTURE_MAX_LINES][ARCLOG_CAPTURE_LINE_LEN];
static uint32_t s_count;

int ArcLog_CaptureEmit(uint32_t vlevel, const char *fmt, ...)
{
    (void)vlevel;
    /* Target formatter (tiny_vsnprintf_like, TINY_PRINTF) has no length
     * modifiers: "%lu" prints literally and shifts the arguments. */
    for (const char *f = strchr(fmt, '%'); f != NULL; f = strchr(f + 1, '%')) {
        const char *c = f + 1;
        while (*c == '0' || (*c >= '1' && *c <= '9')) c++;
        if (*c == 'l' || *c == 'h' || *c == 'z' || *c == 'L') {
            fprintf(stderr, "ArcLog format uses a length modifier the target "
                            "formatter does not support: \"%s\"\n", fmt);
            abort();
        }
    }
    if (s_count >= ARCLOG_CAPTURE_MAX_LINES) {
        return -1;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_lines[s_count], ARCLOG_CAPTURE_LINE_LEN, fmt, args);
    va_end(args);

    /* Drop the trailing "\r\n" so assertions read naturally. */
    char *end = strpbrk(s_lines[s_count], "\r\n");
    if (end != NULL) *end = '\0';

    /* Optional echo for debugging a test: ARCLOG_ECHO=1 ctest -V */
    if (getenv("ARCLOG_ECHO") != NULL) {
        printf("ARCLOG %s\n", s_lines[s_count]);
    }
    s_count++;
    return 0;
}

void ArcLog_CaptureReset(void)
{
    s_count = 0u;
}

uint32_t ArcLog_CaptureCount(void)
{
    return s_count;
}

const char *ArcLog_CaptureLine(uint32_t idx)
{
    return (idx < s_count) ? s_lines[idx] : "";
}

int32_t ArcLog_CaptureFind(const char *needle)
{
    for (uint32_t i = 0u; i < s_count; i++) {
        if (strstr(s_lines[i], needle) != NULL) {
            return (int32_t)i;
        }
    }
    return -1;
}
