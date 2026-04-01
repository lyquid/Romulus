add_subdirectory(cli)
# add_subdirectory(api)  # Future: REST API server

if(ROMULUS_ENABLE_GUI)
    add_subdirectory(gui)
endif()
