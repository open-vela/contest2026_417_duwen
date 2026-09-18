cmake_minimum_required(VERSION 3.16)
project(umca C)

set(UMCA_PROFILE "DISCOVERY" CACHE STRING "UMCA profile (MINIMAL, DISCOVERY, CONTEST)")
set_property(CACHE UMCA_PROFILE PROPERTY STRINGS MINIMAL DISCOVERY CONTEST)
string(TOUPPER "${UMCA_PROFILE}" UMCA_PROFILE_UPPER)

if(NOT UMCA_PROFILE_UPPER STREQUAL "MINIMAL" AND
   NOT UMCA_PROFILE_UPPER STREQUAL "DISCOVERY" AND
   NOT UMCA_PROFILE_UPPER STREQUAL "CONTEST")
  message(FATAL_ERROR "UMCA_PROFILE must be MINIMAL, DISCOVERY, or CONTEST")
endif()

set(UMCA_PROFILE_DEFINE UMCA_PROFILE_${UMCA_PROFILE_UPPER})
set(UMCA_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(UMCA_BASE
  src/base/umca_byteorder.c
  src/base/umca_crc32c.c
  src/base/umca_frame.c
  src/base/umca_topic_id.c)
set(UMCA_CORE
  src/core/umca_context.c
  src/core/umca_process.c
  src/pubsub/umca_topic_table.c
  src/pubsub/umca_publish.c
  src/pubsub/umca_dispatch.c)
set(UMCA_SOURCES ${UMCA_BASE} ${UMCA_CORE})

if(NOT UMCA_PROFILE_UPPER STREQUAL "MINIMAL")
  list(APPEND UMCA_SOURCES
    src/sequence/umca_sequence.c
    src/discovery/umca_discovery.c)
endif()

add_library(umca_core STATIC ${UMCA_SOURCES})
target_include_directories(umca_core PUBLIC ${UMCA_INCLUDE}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_compile_definitions(umca_core PUBLIC ${UMCA_PROFILE_DEFINE})
target_compile_options(umca_core PRIVATE -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wundef)

add_library(umca_phy_loopback STATIC phy/loopback/umca_loopback.c)
target_include_directories(umca_phy_loopback PUBLIC ${UMCA_INCLUDE}
  ${CMAKE_CURRENT_SOURCE_DIR}/phy/loopback)
target_compile_definitions(umca_phy_loopback PUBLIC ${UMCA_PROFILE_DEFINE})
target_compile_options(umca_phy_loopback PRIVATE -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wundef)

add_library(umca_phy_uart STATIC phy/uart/umca_uart.c)
target_include_directories(umca_phy_uart PUBLIC ${UMCA_INCLUDE}
  ${CMAKE_CURRENT_SOURCE_DIR}/phy/uart)
target_compile_definitions(umca_phy_uart PUBLIC ${UMCA_PROFILE_DEFINE})
target_compile_options(umca_phy_uart PRIVATE -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wundef)

add_library(umca_pal_posix STATIC ports/posix/umca_posix_pal.c)
target_include_directories(umca_pal_posix PUBLIC ${UMCA_INCLUDE}
  ${CMAKE_CURRENT_SOURCE_DIR}/ports/posix)
target_compile_definitions(umca_pal_posix PUBLIC ${UMCA_PROFILE_DEFINE})
target_compile_options(umca_pal_posix PRIVATE -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wundef)

include(CTest)
if(BUILD_TESTING)
  add_executable(umca_unit_tests tests/unit/test_umca.c)
  target_link_libraries(umca_unit_tests PRIVATE umca_core)
  target_include_directories(umca_unit_tests PRIVATE ${UMCA_INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
  target_compile_definitions(umca_unit_tests PRIVATE ${UMCA_PROFILE_DEFINE})
  target_compile_options(umca_unit_tests PRIVATE -Wall -Wextra -Wpedantic
    -Wconversion -Wsign-conversion -Wshadow -Wundef)
  add_test(NAME umca_unit_tests COMMAND umca_unit_tests)

  add_executable(umca_demo_codec_tests
    tests/unit/test_demo_codec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/umca_demo/demo_codec.c)
  target_link_libraries(umca_demo_codec_tests PRIVATE umca_core)
  target_include_directories(umca_demo_codec_tests PRIVATE ${UMCA_INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/umca_demo)
  target_compile_definitions(umca_demo_codec_tests PRIVATE ${UMCA_PROFILE_DEFINE})
  target_compile_options(umca_demo_codec_tests PRIVATE -Wall -Wextra -Wpedantic
    -Wconversion -Wsign-conversion -Wshadow -Wundef)
  add_test(NAME umca_demo_codec_tests COMMAND umca_demo_codec_tests)

  add_executable(umca_uart_phy_tests tests/unit/test_uart_phy.c)
  target_link_libraries(umca_uart_phy_tests PRIVATE umca_core umca_phy_uart)
  target_include_directories(umca_uart_phy_tests PRIVATE ${UMCA_INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}/phy/uart)
  target_compile_definitions(umca_uart_phy_tests PRIVATE ${UMCA_PROFILE_DEFINE})
  target_compile_options(umca_uart_phy_tests PRIVATE -Wall -Wextra -Wpedantic
    -Wconversion -Wsign-conversion -Wshadow -Wundef)
  add_test(NAME umca_uart_phy_tests COMMAND umca_uart_phy_tests)

  if(NOT UMCA_PROFILE_UPPER STREQUAL "MINIMAL")
    add_executable(umca_host_three_nodes tests/integration/test_three_nodes.c)
    target_link_libraries(umca_host_three_nodes PRIVATE umca_core
      umca_phy_loopback)
    target_include_directories(umca_host_three_nodes PRIVATE ${UMCA_INCLUDE}
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core
      ${CMAKE_CURRENT_SOURCE_DIR}/phy/loopback)
    target_compile_definitions(umca_host_three_nodes PRIVATE ${UMCA_PROFILE_DEFINE})
    target_compile_options(umca_host_three_nodes PRIVATE -Wall -Wextra -Wpedantic
      -Wconversion -Wsign-conversion -Wshadow -Wundef)
    if(UMCA_PROFILE_UPPER STREQUAL "CONTEST")
      find_package(Threads REQUIRED)
      target_link_libraries(umca_host_three_nodes PRIVATE Threads::Threads)
    endif()
    add_test(NAME umca_host_three_nodes COMMAND umca_host_three_nodes)

    add_executable(umca_gd32_agent_tests
      tests/integration/test_gd32_agent.c
      ${CMAKE_CURRENT_SOURCE_DIR}/../app/openvela_umca_gd32/umca_gd32_agent.c
      ${CMAKE_CURRENT_SOURCE_DIR}/../app/openvela_umca_gd32/umca_ai_agent_registry.c
      ${CMAKE_CURRENT_SOURCE_DIR}/../app/umca_demo/demo_codec.c)
    target_link_libraries(umca_gd32_agent_tests PRIVATE umca_core
      umca_phy_loopback)
    target_include_directories(umca_gd32_agent_tests PRIVATE ${UMCA_INCLUDE}
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core
      ${CMAKE_CURRENT_SOURCE_DIR}/phy/loopback
      ${CMAKE_CURRENT_SOURCE_DIR}/../app/openvela_umca_gd32
      ${CMAKE_CURRENT_SOURCE_DIR}/../app/umca_demo
      ${CMAKE_CURRENT_SOURCE_DIR}/../../packages/ai_agent/src
      ${CMAKE_CURRENT_SOURCE_DIR}/../../packages/ai_agent/include)
    target_compile_definitions(umca_gd32_agent_tests PRIVATE
      ${UMCA_PROFILE_DEFINE})
    target_compile_options(umca_gd32_agent_tests PRIVATE -Wall -Wextra
      -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wundef)
    if(UMCA_PROFILE_UPPER STREQUAL "CONTEST")
      target_link_libraries(umca_gd32_agent_tests PRIVATE Threads::Threads)
    endif()
    add_test(NAME umca_gd32_agent_tests COMMAND umca_gd32_agent_tests)
  endif()
endif()
