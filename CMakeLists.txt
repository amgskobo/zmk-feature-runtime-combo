zephyr_include_directories(include)
if(CONFIG_ZMK_RUNTIME_COMBO)
    target_sources(app PRIVATE src/runtime_combo/runtime_combo.c)

    if(CONFIG_ZMK_RUNTIME_COMBO_STUDIO_RPC)
        target_sources(app PRIVATE src/studio/runtime_combo_handler.c)

        list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
        include(nanopb)
        set(NANOPB_GENERATE_CPP_APPEND_PATH TRUE)
        set(NANOPB_GENERATE_CPP_STANDALONE OFF)

        # NOTE: adding to app target directly causes build issues, so we create a library instead
        zephyr_library()
        file(GLOB_RECURSE PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/proto/*.proto)

        # FindNanopb configures .options.in in the build tree. Keep the ordinary
        # .options file as the protocol-16 standalone-generator fallback.
        set(RUNTIME_COMBO_NANOPB_OPTIONS_FILE
            ${CMAKE_CURRENT_SOURCE_DIR}/proto/cormoran/runtime_combo/runtime_combo.options)
        file(READ ${RUNTIME_COMBO_NANOPB_OPTIONS_FILE} RUNTIME_COMBO_NANOPB_OPTIONS)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                     ${RUNTIME_COMBO_NANOPB_OPTIONS_FILE})

        nanopb_generate_cpp(proto_srcs proto_hdrs RELPATH ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_FILES})
        target_include_directories(${ZEPHYR_CURRENT_LIBRARY} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
        target_sources(${ZEPHYR_CURRENT_LIBRARY} PRIVATE ${proto_srcs} ${proto_hdrs})

        # app should depend on the generated proto files
        target_include_directories(app PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/proto)
        add_dependencies(app ${ZEPHYR_CURRENT_LIBRARY})
    endif()
endif()
