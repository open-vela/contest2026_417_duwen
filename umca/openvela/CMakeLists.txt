# UMCA openvela package integration.

if(CONFIG_UMCA)
  nuttx_add_library(umca_core)

  # The manifest exposes this package through a symlink under apps/.  Resolve
  # it before walking to the platform-independent UMCA sources; otherwise
  # CMake would incorrectly look below apps/packages/demos/.
  get_filename_component(UMCA_PACKAGE_DIR ${CMAKE_CURRENT_LIST_DIR} REALPATH)
  set(UMCA_SRC_DIR ${UMCA_PACKAGE_DIR}/..)
  set(UMCA_OPENVELA_SOURCES
    ${UMCA_SRC_DIR}/src/base/umca_byteorder.c
    ${UMCA_SRC_DIR}/src/base/umca_crc32c.c
    ${UMCA_SRC_DIR}/src/base/umca_frame.c
    ${UMCA_SRC_DIR}/src/base/umca_topic_id.c
    ${UMCA_SRC_DIR}/src/core/umca_context.c
    ${UMCA_SRC_DIR}/src/core/umca_process.c
    ${UMCA_SRC_DIR}/src/pubsub/umca_topic_table.c
    ${UMCA_SRC_DIR}/src/pubsub/umca_publish.c
    ${UMCA_SRC_DIR}/src/pubsub/umca_dispatch.c)

  if(CONFIG_UMCA_PROFILE_DISCOVERY OR CONFIG_UMCA_PROFILE_CONTEST)
    list(APPEND UMCA_OPENVELA_SOURCES
      ${UMCA_SRC_DIR}/src/sequence/umca_sequence.c
      ${UMCA_SRC_DIR}/src/discovery/umca_discovery.c)
  endif()

  target_sources(umca_core PRIVATE ${UMCA_OPENVELA_SOURCES})
  target_include_directories(umca_core PUBLIC ${UMCA_SRC_DIR}/include
    ${UMCA_SRC_DIR}/src/core)

  if(CONFIG_UMCA_PROFILE_MINIMAL)
    target_compile_definitions(umca_core PUBLIC UMCA_PROFILE_MINIMAL)
  elseif(CONFIG_UMCA_PROFILE_CONTEST)
    target_compile_definitions(umca_core PUBLIC UMCA_PROFILE_CONTEST)
  else()
    target_compile_definitions(umca_core PUBLIC UMCA_PROFILE_DISCOVERY)
  endif()

  if(CONFIG_UMCA_BUILD_LOOPBACK)
    nuttx_add_library(umca_phy_loopback)
    target_sources(umca_phy_loopback PRIVATE
      ${UMCA_SRC_DIR}/phy/loopback/umca_loopback.c)
    target_include_directories(umca_phy_loopback PUBLIC
      ${UMCA_SRC_DIR}/include ${UMCA_SRC_DIR}/phy/loopback)
    target_link_libraries(umca_phy_loopback PUBLIC umca_core)
  endif()

  if(CONFIG_UMCA_BUILD_UART)
    nuttx_add_library(umca_phy_uart)
    target_sources(umca_phy_uart PRIVATE
      ${UMCA_SRC_DIR}/phy/uart/umca_uart.c
      ${UMCA_SRC_DIR}/ports/openvela/umca_openvela_uart.c)
    target_include_directories(umca_phy_uart PUBLIC
      ${UMCA_SRC_DIR}/include ${UMCA_SRC_DIR}/phy/uart
      ${UMCA_SRC_DIR}/ports/openvela)
    target_link_libraries(umca_phy_uart PUBLIC umca_core)
  endif()

  nuttx_add_library(umca_openvela_pal)
  target_sources(umca_openvela_pal PRIVATE
    ${UMCA_SRC_DIR}/ports/openvela/umca_openvela_pal.c)
  target_include_directories(umca_openvela_pal PUBLIC
    ${UMCA_SRC_DIR}/include ${UMCA_SRC_DIR}/ports/openvela)
endif()
