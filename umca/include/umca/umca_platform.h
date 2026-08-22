#ifndef UMCA_PLATFORM_H
#define UMCA_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
  uint32_t (*time_ms)(void *user);
  uint64_t (*time_us)(void *user);
  uintptr_t (*critical_enter)(void *user);
  void (*critical_exit)(void *user, uintptr_t state);
  int (*mutex_lock)(void *user, void *mutex);
  int (*mutex_unlock)(void *user, void *mutex);
  void *(*alloc)(void *user, size_t size);
  void (*free)(void *user, void *ptr);
  void (*log)(void *user, int level, const char *message);
  void (*assert_fail)(void *user, const char *file, int line,
                      const char *expr);
} umca_platform_ops_t;

typedef struct
{
  const umca_platform_ops_t *ops;
  void *user;
  void *mutex;
} umca_platform_t;

#endif
