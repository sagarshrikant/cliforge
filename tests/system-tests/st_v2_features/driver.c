/* Behavioural test driver for the v2 feature fixture. */
#include "cmdline.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    struct v2_cmdline a;
    unsigned int i;
    char *p = (char *)&a;
    for (i = 0U; i < sizeof(a); i++) p[i] = 0;

    if (v2_cmdline_parse(argc, argv, &a) != 0) {
        printf("PARSE_FAIL\n");
        return 2;
    }
    printf("verbosity=%d\n", (int)a.verbosity);
    printf("timeout_ns=%lu\n", (unsigned long)v2_duration_to_ns(&a.timeout));
    printf("retries=%d\n", (int)a.retries);
    printf("job.level=%d\n", (int)a.job.level);
    printf("job.mode=%d\n", (int)a.job.mode);
    printf("job.period_ns=%lu\n", (unsigned long)v2_duration_to_ns(&a.job.period));
    printf("job.size_bytes=%lu\n", (unsigned long)v2_bytes_to_bytes(&a.job.size));
    return 0;
}
