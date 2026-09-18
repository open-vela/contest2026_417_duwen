cmake_minimum_required(VERSION 3.16)
project(umca_node_sdk C)

set(UMCA_PROFILE "DISCOVERY" CACHE STRING
    "UMCA profile (MINIMAL, DISCOVERY, CONTEST)")
set_property(CACHE UMCA_PROFILE PROPERTY STRINGS MINIMAL DISCOVERY CONTEST)
string(TOUPPER "${UMCA_PROFILE}" UMCA_PROFILE_UPPER)

if(NOT UMCA_PROFILE_UPPER STREQUAL "MINIMAL" AND
   NOT UMCA_PROFILE_UPPER STREQUAL "DISCOVERY" AND
   NOT UMCA_PROFILE_UPPER STREQUAL "CONTEST")
  message(FATAL_ERROR
          "UMCA_PROFILE must be MINIMAL, DISCOVERY, or CONTEST")
endif()

set(UMCA_PROFILE_DEFINE UMCA_PROFILE_${UMCA_PROFILE_UPPER})
set(UMCA_PUBLIC_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(UMCA_CORE_SOURCES
  src/base/umca_byteorder.c
  src/base/umca_crc32c.c
  src/base/umca_frame.c
  src/base/umca_topic_id.c
  src/core/umca_context.c
  src/core/umca_process.c
  src/pubsub/umca_topic_table.c
  src/pubsub/umca_publish.c
  src/pubsub/umca_dispatch.c)

if(NOT UMCA_PROFILE_UPPER STREQUAL "MINIMAL")
  list(APPEND UMCA_CORE_SOURCES
    src/sequence/umca_sequence.c
    src/discovery/umca_discovery.c)
endif()

add_library(umca_core STATIC ${UMCA_CORE_SOURCES})
target_include_directories(umca_core
  PUBLIC ${UMCA_PUBLIC_INCLUDE}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_compile_definitions(umca_core PUBLIC ${UMCA_PROFILE_DEFINE})
target_compile_options(umca_core PRIVATE -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wundef)

add_library(umca_phy_uart STATIC phy/uart/umca_uart.c)
target_include_directories(umca_phy_uart
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/phy/uart ${UMCA_PUBLIC_INCLUDE})
target_compile_definitions(umca_phy_uart PUBLIC ${UMCA_PROFILE_DEFINE})
target_compile_options(umca_phy_uart PRIVATE -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wundef)

add_library(umca_phy_loopback STATIC phy/loopback/umca_loopback.c)
target_include_directories(umca_phy_loopback
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/phy/loopback ${UMCA_PUBLIC_INCLUDE})
target_compile_definitions(umca_phy_loopback PUBLIC ${UMCA_PROFILE_DEFINE})
target_compile_options(umca_phy_loopback PRIVATE -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wundef)

add_library(umca_pal_posix STATIC ports/posix/umca_posix_pal.c)
target_include_directories(umca_pal_posix
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ports/posix ${UMCA_PUBLIC_INCLUDE})
target_compile_definitions(umca_pal_posix PUBLIC ${UMCA_PROFILE_DEFINE})
target_compile_options(umca_pal_posix PRIVATE -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wundef)

add_library(umca_demo_codec STATIC demo/demo_codec.c)
target_include_directories(umca_demo_codec
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/demo ${UMCA_PUBLIC_INCLUDE})
target_compile_definitions(umca_demo_codec PUBLIC ${UMCA_PROFILE_DEFINE})
target_compile_options(umca_demo_codec PRIVATE -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion -Wshadow -Wundef)
target_link_libraries(umca_demo_codec PUBLIC umca_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(umca_unit_tests tests/unit/test_umca.c)
  target_include_directories(umca_unit_tests PRIVATE src/core)
  target_link_libraries(umca_unit_tests PRIVATE umca_core)
  add_test(NAME umca_unit_tests COMMAND umca_unit_tests)

  add_executable(umca_demo_codec_tests tests/unit/test_demo_codec.c)
  target_link_libraries(umca_demo_codec_tests PRIVATE umca_demo_codec)
  add_test(NAME umca_demo_codec_tests COMMAND umca_demo_codec_tests)

  add_executable(umca_uart_phy_tests tests/unit/test_uart_phy.c)
  target_link_libraries(umca_uart_phy_tests PRIVATE umca_core umca_phy_uart)
  add_test(NAME umca_uart_phy_tests COMMAND umca_uart_phy_tests)

  if(NOT UMCA_PROFILE_UPPER STREQUAL "MINIMAL")
    add_executable(umca_host_three_nodes tests/integration/test_three_nodes.c)
    target_include_directories(umca_host_three_nodes PRIVATE src/core)
    target_link_libraries(umca_host_three_nodes PRIVATE umca_core
      umca_phy_loopback)
    if(UMCA_PROFILE_UPPER STREQUAL "CONTEST")
      find_package(Threads REQUIRED)
      target_link_libraries(umca_host_three_nodes PRIVATE Threads::Threads)
    endif()
    add_test(NAME umca_host_three_nodes COMMAND umca_host_three_nodes)
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS umca_core umca_phy_uart umca_demo_codec
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES phy/uart/umca_uart.h demo/demo_topics.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
