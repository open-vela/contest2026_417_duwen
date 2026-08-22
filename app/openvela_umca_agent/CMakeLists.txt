if(CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_AGENT)
  get_filename_component(UMCA_AGENT_DIR ${CMAKE_CURRENT_LIST_DIR} REALPATH)
  get_filename_component(UMCA_REPO_DIR ${UMCA_AGENT_DIR}/../.. REALPATH)
  nuttx_add_application(
    NAME umca_agent
    SRCS demo/demo_codec.c adapter/umca_agent_adapter.c
         service/umca_service.c umca_agent_main.c
    STACKSIZE 16384)
  target_include_directories(apps_umca_agent PRIVATE
    ${UMCA_AGENT_DIR}
    ${UMCA_AGENT_DIR}/demo
    ${UMCA_AGENT_DIR}/adapter
    ${UMCA_AGENT_DIR}/service
    ${NUTTX_DIR}/../packages/ai_agent/include
    ${NUTTX_DIR}/../packages/ai_agent/src
    ${UMCA_REPO_DIR}/umca/include)
  target_link_libraries(apps_umca_agent PRIVATE
    umca_core umca_phy_loopback umca_openvela_pal)

  # Goldfish's base apps Kconfig does not aggregate packages/ai_agent.  Pull
  # the existing ai_agent application into the same image at the integration
  # layer so the weak UMCA provider hooks resolve at runtime.  UMCA Core and
  # its protocol/PHY libraries remain independent of ai_agent.
  if(EXISTS ${NUTTX_DIR}/../packages/ai_agent/CMakeLists.txt)
    set(CONFIG_EXAMPLES_AI_AGENT_VELA ON)
    set(CONFIG_EXAMPLES_AI_AGENT_VELA_PROGNAME ai_agent)
    set(CONFIG_EXAMPLES_AI_AGENT_VELA_STACKSIZE 32768)
    set(CONFIG_EXAMPLES_AI_AGENT_VELA_PRIORITY 100)
    add_subdirectory(${NUTTX_DIR}/../packages/ai_agent
                     ${CMAKE_BINARY_DIR}/apps/packages/ai_agent_umca)
  endif()
endif()
