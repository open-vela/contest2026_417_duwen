#include <time.h>

#include "umca_posix_pal.h"

static uint32_t posix_time_ms(void *user)
{
  struct timespec ts;
  (void)user;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                    (uint64_t)ts.tv_nsec / 1000000u);
}

static uint64_t posix_time_us(void *user)
{
  struct timespec ts;
  (void)user;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static const umca_platform_ops_t g_posix_ops =
{
  posix_time_ms,
  posix_time_us,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

umca_platform_t umca_posix_platform(void)
{
  umca_platform_t platform = {&g_posix_ops, NULL, NULL};
  return platform;
}
