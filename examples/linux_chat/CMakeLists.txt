add_executable(micronet_chat
    main.c
)

target_link_libraries(micronet_chat PRIVATE micronet)

if(COMMAND micronet_enable_warnings)
    micronet_enable_warnings(micronet_chat)
endif()
