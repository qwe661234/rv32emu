#pragma once

#include <sys/time.h>
#include <time.h>

/* Obtain the system 's notion of the current Greenwich time.
 * TODO: manipulate current time zone.
 */
void rv_gettimeofday(struct timeval *tv);

/* Retrieve the value used by a clock which is specified by clock_id. */
void rv_clock_gettime(struct timespec *tp);

#if defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>

static void *malloc_exec(uint32_t page_count)
{
    void *block = mmap(0, getpagesize() * page_count, PROT_WRITE | PROT_EXEC,
                       MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    return block;
}
#endif