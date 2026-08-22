#include <time.h>

#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/spinlock.h>

#include "umca_openvela_pal.h"

static uint32_t openvela_time_ms(void *user)
{
  struct timespec ts;
  (void)user;
  (void)clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                    (uint64_t)ts.tv_nsec / 1000000u);
}

static uint64_t openvela_time_us(void *user)
{
  struct timespec ts;
  (void)user;
  (void)clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static uintptr_t openvela_critical_enter(void *user)
{
  (void)user;
  return (uintptr_t)enter_critical_section();
}

static void openvela_critical_exit(void *user, uintptr_t state)
{
  (void)user;
  leave_critical_section((irqstate_t)state);
}

static int openvela_mutex_lock(void *user, void *mutex)
{
  (void)user;
  return nxmutex_lock((mutex_t *)mutex);
}

static int openvela_mutex_unlock(void *user, void *mutex)
{
  (void)user;
  return nxmutex_unlock((mutex_t *)mutex);
}

static const umca_platform_ops_t g_openvela_ops =
{
  openvela_time_ms,
  openvela_time_us,
  openvela_critical_enter,
  openvela_critical_exit,
  openvela_mutex_lock,
  openvela_mutex_unlock,
  NULL,
  NULL,
  NULL,
  NULL
};

umca_platform_t umca_openvela_platform(void *mutex)
{
  umca_platform_t platform = {&g_openvela_ops, NULL, mutex};
  return platform;
}
