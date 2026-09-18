if(CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32)
  nuttx_add_application(
    NAME ${CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_PROGNAME}
    SRCS umca_gd32_main.c umca_gd32_agent.c umca_ai_agent_registry.c
         ../umca_demo/demo_codec.c
    STACKSIZE ${CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_STACKSIZE}
    PRIORITY ${CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_PRIORITY})
  target_link_libraries(apps_${CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_PROGNAME}
    PRIVATE umca_core umca_phy_uart umca_openvela_pal)
  target_include_directories(
    apps_${CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_PROGNAME}
    PRIVATE ../umca_demo)
  target_include_directories(
    apps_${CONFIG_LVX_USE_DEMO_CONTEST2026_417_UMCA_GD32_PROGNAME}
    PRIVATE ${CMAKE_SOURCE_DIR}/../packages/ai_agent/src
            ${CMAKE_SOURCE_DIR}/../packages/ai_agent/include)
endif()
