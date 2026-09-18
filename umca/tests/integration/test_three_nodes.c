#include <assert.h>
#include <string.h>
#if defined(UMCA_PROFILE_CONTEST)
#  include <pthread.h>
#endif

#include "umca/umca.h"
#include "umca_loopback.h"

typedef struct
{
  unsigned int received;
} callback_state_t;

static uint32_t fake_time;
static uint32_t time_ms(void *user)
{
  (void)user;
  return fake_time;
}

#if defined(UMCA_PROFILE_CONTEST)
static int mutex_lock(void *user, void *mutex)
{
  (void)user;
  return pthread_mutex_lock((pthread_mutex_t *)mutex);
}

static int mutex_unlock(void *user, void *mutex)
{
  (void)user;
  return pthread_mutex_unlock((pthread_mutex_t *)mutex);
}
#endif

static const umca_platform_ops_t platform_ops =
{
  time_ms, NULL, NULL, NULL,
#if defined(UMCA_PROFILE_CONTEST)
  mutex_lock, mutex_unlock,
#else
  NULL, NULL,
#endif
  NULL, NULL, NULL, NULL
};

static void on_data(umca_context_t *ctx, const umca_message_t *message,
                    void *user)
{
  callback_state_t *state = user;
  (void)ctx;
  assert(message->payload_length == 3);
  assert(memcmp(message->payload, "tmp", 3) == 0);
  state->received++;
}

int main(void)
{
  umca_loopback_bus_t bus;
  umca_loopback_endpoint_t ep[3];
  umca_phy_t phy[3];
  umca_platform_t platform = {&platform_ops, NULL, NULL};
  umca_context_t ctx[3];
  umca_node_info_t node;
  callback_state_t callback = {0};
  umca_topic_id_t topic;
  uint16_t processed;
  unsigned int i;
#if defined(UMCA_PROFILE_CONTEST)
  pthread_mutex_t mutex;
  assert(pthread_mutex_init(&mutex, NULL) == 0);
  platform.mutex = &mutex;
#endif
  umca_loopback_bus_init(&bus);
  for (i = 0; i < 3; i++)
    {
      assert(umca_loopback_endpoint_init(&bus, &ep[i]) == 0);
      phy[i] = umca_loopback_phy(&ep[i]);
    }
  assert(umca_init(&ctx[0], UINT64_C(0x1001), UINT32_C(0x10010001),
                   &platform, &phy[0]) == UMCA_OK);
  assert(umca_init(&ctx[1], UINT64_C(0x2001), UINT32_C(0x20010001),
                   &platform, &phy[1]) == UMCA_OK);
  assert(umca_init(&ctx[2], UINT64_C(0x3001), UINT32_C(0x30010001),
                   &platform, &phy[2]) == UMCA_OK);
  assert(umca_topic_register(&ctx[0], "/sensors/temperature",
                             UMCA_TOPIC_PUBLISHER, NULL, NULL, &topic) ==
         UMCA_OK);
  assert(umca_topic_register(&ctx[1], "/sensors/temperature",
                             UMCA_TOPIC_SUBSCRIBER, on_data, &callback,
                             &topic) == UMCA_OK);
  assert(umca_start(&ctx[0]) == UMCA_OK);
  assert(umca_start(&ctx[1]) == UMCA_OK);
  assert(umca_start(&ctx[2]) == UMCA_OK);
  for (i = 0; i < 3; i++)
    {
      (void)umca_poll(&ctx[i], &processed);
    }
  assert(umca_node_get(&ctx[1], UINT64_C(0x1001), &node) == UMCA_OK);
  assert(node.topic_count == 1);
  assert(node.topics[0].topic_id == topic);
  assert(node.topics[0].direction == UMCA_TOPIC_PUBLISHER);
  assert(node.topics[0].name_length == strlen("/sensors/temperature"));
  assert(strcmp(node.topics[0].name, "/sensors/temperature") == 0);
  assert(umca_publish(&ctx[0], topic, UINT64_C(0x2001),
                      (const uint8_t *)"tmp", 3) == UMCA_OK);
  (void)umca_poll(&ctx[1], &processed);
  assert(callback.received == 1);
  umca_loopback_duplicate_next(&bus);
  assert(umca_publish(&ctx[0], topic, UINT64_C(0x2001),
                      (const uint8_t *)"tmp", 3) == UMCA_OK);
  assert(umca_poll(&ctx[1], &processed) == UMCA_ERR_DUPLICATE);
  assert(callback.received == 2);
  umca_loopback_corrupt_next(&bus, 40);
  assert(umca_publish(&ctx[0], topic, UINT64_C(0x2001),
                      (const uint8_t *)"tmp", 3) == UMCA_OK);
  assert(umca_poll(&ctx[1], &processed) == UMCA_ERR_CRC);
  assert(callback.received == 2);
  fake_time = 16000;
  (void)umca_poll(&ctx[1], &processed);
  assert(umca_node_count(&ctx[1], UMCA_NODE_OFFLINE) >= 1);
  for (i = 0; i < 3; i++)
    {
      umca_deinit(&ctx[i]);
    }
#if defined(UMCA_PROFILE_CONTEST)
  assert(pthread_mutex_destroy(&mutex) == 0);
#endif
  return 0;
}
