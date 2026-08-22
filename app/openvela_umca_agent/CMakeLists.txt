if(CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_AGENT)
  nuttx_add_application(
    NAME umca_agent
    SRCS demo/demo_codec.c adapter/umca_agent_adapter.c umca_agent_main.c
    STACKSIZE 4096)
  target_include_directories(apps_umca_agent PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/demo
    ${CMAKE_CURRENT_LIST_DIR}/adapter
    ${NUTTX_DIR}/../packages/ai_agent/include
    ${CMAKE_CURRENT_LIST_DIR}/../../umca/include)
endif()
