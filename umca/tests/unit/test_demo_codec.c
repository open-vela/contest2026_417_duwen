#include <assert.h>
#include "demo_topics.h"

int main(void)
{
  demo_temperature_sample_t sample = {30500, 1000, 1};
  demo_temperature_sample_t decoded;
  uint8_t payload[13];
  assert(demo_encode_temperature_sample(&sample, payload) == 0);
  assert(demo_decode_temperature_sample(payload, 9, &decoded) == 0);
  assert(decoded.temperature_mc == sample.temperature_mc &&
         decoded.sample_time_ms == sample.sample_time_ms &&
         decoded.quality == sample.quality);
  assert(demo_decode_temperature_sample(payload, 8, &decoded) != 0);

  {
    demo_fan_command_t command = {42, 1, 1};
    demo_fan_command_t command_decoded;
    assert(demo_encode_fan_command(&command, payload) == 0);
    assert(demo_decode_fan_command(payload, 6, &command_decoded) == 0);
    assert(command.request_id == command_decoded.request_id &&
           command.command == command_decoded.command &&
           command.source == command_decoded.source);
  }
  return 0;
}
