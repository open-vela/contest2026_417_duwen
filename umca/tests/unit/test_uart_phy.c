#include <assert.h>
#include <string.h>

#include "umca/umca_protocol.h"
#include "umca_uart.h"

typedef struct
{
  const uint8_t *input;
  size_t input_length;
  size_t input_offset;
  size_t max_read;
  uint8_t output[UMCA_MAX_FRAME * 2u];
  size_t output_length;
  size_t max_write;
} mock_uart_t;

static int mock_read(void *user, uint8_t *buffer, size_t capacity,
                     size_t *received)
{
  mock_uart_t *mock = user;
  size_t length = mock->input_length - mock->input_offset;
  if (length > capacity)
    {
      length = capacity;
    }
  if (length > mock->max_read)
    {
      length = mock->max_read;
    }
  memcpy(buffer, mock->input + mock->input_offset, length);
  mock->input_offset += length;
  *received = length;
  return 0;
}

static int mock_write(void *user, const uint8_t *buffer, size_t length,
                      size_t *sent)
{
  mock_uart_t *mock = user;
  size_t count = length;
  if (count > mock->max_write)
    {
      count = mock->max_write;
    }
  memcpy(mock->output + mock->output_length, buffer, count);
  mock->output_length += count;
  *sent = count;
  return 0;
}

static size_t make_frame(uint8_t *output, uint8_t sequence)
{
  umca_frame_header_t header =
  {
    0, UMCA_MSG_DATA, 0, UMCA_DEFAULT_TTL, UMCA_BROADCAST_DEVID,
    UINT64_C(0x1001), UINT32_C(0x12345678), sequence, 1
  };
  uint8_t payload = sequence;
  size_t length = 0;
  assert(umca_frame_encode(&header, &payload, output, UMCA_MAX_FRAME,
                           &length) == UMCA_OK);
  return length;
}

static void verify_stream(size_t max_read)
{
  uint8_t frames[UMCA_MAX_FRAME * 2u];
  uint8_t first[UMCA_MAX_FRAME];
  uint8_t second[UMCA_MAX_FRAME];
  uint8_t received[UMCA_MAX_FRAME];
  size_t first_length = make_frame(first, 1);
  size_t second_length = make_frame(second, 2);
  size_t received_length;
  mock_uart_t mock;
  umca_uart_io_t io;
  umca_uart_t uart;
  umca_phy_t phy;

  memcpy(frames, first, first_length);
  memcpy(frames + first_length, second, second_length);
  memset(&mock, 0, sizeof(mock));
  mock.input = frames;
  mock.input_length = first_length + second_length;
  mock.max_read = max_read;
  mock.max_write = sizeof(mock.output);
  io.read = mock_read;
  io.write = mock_write;
  io.user = &mock;
  assert(umca_uart_init(&uart, &io) == 0);
  phy = umca_uart_phy(&uart);
  assert(phy.ops->init(phy.driver) == 0);
  assert(phy.ops->poll(phy.driver, received, sizeof(received),
                       &received_length) == 0);
  assert(received_length == first_length);
  assert(memcmp(received, first, first_length) == 0);
  assert(phy.ops->poll(phy.driver, received, sizeof(received),
                       &received_length) == 0);
  assert(received_length == second_length);
  assert(memcmp(received, second, second_length) == 0);
  assert(phy.ops->poll(phy.driver, received, sizeof(received),
                       &received_length) == 0);
  assert(received_length == 0);
  phy.ops->deinit(phy.driver);
}

static void verify_short_writes(void)
{
  uint8_t frame[UMCA_MAX_FRAME];
  size_t frame_length = make_frame(frame, 3);
  size_t max_write;

  for (max_write = 1; max_write <= frame_length; max_write++)
    {
      mock_uart_t mock;
      umca_uart_io_t io;
      umca_uart_t uart;
      umca_phy_t phy;

      memset(&mock, 0, sizeof(mock));
      mock.max_read = 1;
      mock.max_write = max_write;
      io.read = mock_read;
      io.write = mock_write;
      io.user = &mock;
      assert(umca_uart_init(&uart, &io) == 0);
      phy = umca_uart_phy(&uart);
      assert(phy.ops->send(phy.driver, frame, frame_length) == 0);
      assert(mock.output_length == frame_length);
      assert(memcmp(mock.output, frame, frame_length) == 0);
    }
}

static void verify_resync_and_capacity(void)
{
  uint8_t frame[UMCA_MAX_FRAME];
  uint8_t input[UMCA_MAX_FRAME + 4u];
  uint8_t received[UMCA_MAX_FRAME];
  const uint8_t noise[4] = {0xaau, 0x55u, 0x00u, 0x55u};
  size_t frame_length = make_frame(frame, 4);
  size_t received_length;
  mock_uart_t mock;
  umca_uart_io_t io;
  umca_uart_t uart;
  umca_phy_t phy;

  memcpy(input, noise, sizeof(noise));
  memcpy(input + sizeof(noise), frame, frame_length);
  memset(&mock, 0, sizeof(mock));
  mock.input = input;
  mock.input_length = sizeof(noise) + frame_length;
  mock.max_read = 3;
  mock.max_write = sizeof(mock.output);
  io.read = mock_read;
  io.write = mock_write;
  io.user = &mock;
  assert(umca_uart_init(&uart, &io) == 0);
  phy = umca_uart_phy(&uart);
  assert(phy.ops->poll(phy.driver, received, 1, &received_length) != 0);
  assert(received_length == 0);
  assert(phy.ops->poll(phy.driver, received, sizeof(received),
                       &received_length) == 0);
  assert(received_length == frame_length);
  assert(memcmp(received, frame, frame_length) == 0);
}

int main(void)
{
  size_t max_read;

  for (max_read = 1; max_read <= 64; max_read++)
    {
      verify_stream(max_read);
    }
  verify_short_writes();
  verify_resync_and_capacity();
  return 0;
}
