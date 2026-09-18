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
  {
    static const uint8_t text[] = "turn fan on";
    demo_llm_chat_request_t request =
      {
        UINT32_C(0x47000001), 7, 15000, DEMO_LLM_ORIGIN_TERMINAL,
        (uint16_t)(sizeof(text) - 1u), text
      };
    demo_llm_chat_request_t request_decoded;
    uint8_t chat_payload[256];
    uint16_t chat_length;
    assert(demo_encode_llm_chat_request(&request, chat_payload,
                                        &chat_length) == 0);
    assert(chat_length == DEMO_LLM_CHAT_REQUEST_HEADER_SIZE +
                          sizeof(text) - 1u);
    assert(demo_decode_llm_chat_request(chat_payload, chat_length,
                                        &request_decoded) == 0);
    assert(request_decoded.requester_boot_id == request.requester_boot_id);
    assert(request_decoded.request_id == request.request_id);
    assert(request_decoded.timeout_ms == request.timeout_ms);
    assert(request_decoded.text_length == request.text_length);
  }
  {
    static const uint8_t text[] = "accepted";
    demo_llm_chat_response_t response =
      {
        UINT32_C(0x47000001), 7, DEMO_LLM_STATUS_OK,
        DEMO_LLM_ACTION_SET_FAN, 1, (uint16_t)(sizeof(text) - 1u), text
      };
    demo_llm_chat_response_t response_decoded;
    uint8_t chat_payload[256];
    uint16_t chat_length;
    assert(demo_encode_llm_chat_response(&response, chat_payload,
                                         &chat_length) == 0);
    assert(demo_decode_llm_chat_response(chat_payload, chat_length,
                                         &response_decoded) == 0);
    assert(response_decoded.requester_boot_id == response.requester_boot_id);
    assert(response_decoded.request_id == response.request_id);
    assert(response_decoded.action_type == DEMO_LLM_ACTION_SET_FAN);
    assert(response_decoded.action_value == 1);
    response.action_type = DEMO_LLM_ACTION_NONE;
    assert(demo_encode_llm_chat_response(&response, chat_payload,
                                         &chat_length) != 0);
  }
  return 0;
}
