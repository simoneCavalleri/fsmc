# ==============================================================================
# FsmcTools.cmake / FsmGenTools.cmake - CMake helper module for fsmc code generator
# ==============================================================================
#
# Provides the function:
#   fsmc_target_sources(<target>
#       DIAGRAMS <diagram1> [<diagram2>...]
#       [NAME <class_name>]
#       [STANDARD <17|20>]
#       [NAMESPACE <namespace>]
#       [STANDALONE]
#       [OUTPUT_DIR <dir>]
#   )
#
# Automatically runs fsmc to generate C++ state machine headers and adds them
# to the specified CMake target.
# ==============================================================================

function(fsmc_target_sources TARGET_NAME)
    set(options STANDALONE MODULAR NO_THREAD_SAFE NO_STUBS)
    set(oneValueArgs NAME STANDARD NAMESPACE OUTPUT_DIR FORMAT TARGET_LANG)
    set(multiValueArgs DIAGRAMS)

    cmake_parse_arguments(FSM_ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT FSM_ARG_DIAGRAMS)
        message(FATAL_ERROR "fsmc_target_sources: No DIAGRAMS specified for target ${TARGET_NAME}")
    endif()

    if(NOT FSM_ARG_OUTPUT_DIR)
        set(FSM_ARG_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_${TARGET_NAME}")
    endif()

    file(MAKE_DIRECTORY "${FSM_ARG_OUTPUT_DIR}")

    # Determine fsmc executable location (either built in-tree or from system)
    if(TARGET fsmc)
        set(FSMC_EXE "$<TARGET_FILE:fsmc>")
        set(FSMC_DEP fsmc)
    else()
        find_program(FSMC_EXE NAMES fsmc REQUIRED)
        set(FSMC_DEP "${FSMC_EXE}")
    endif()

    set(GENERATED_HEADERS "")

    foreach(DIAGRAM_PATH IN LISTS FSM_ARG_DIAGRAMS)
        get_filename_component(DIAGRAM_NAME "${DIAGRAM_PATH}" NAME_WE)
        get_filename_component(DIAGRAM_ABS "${DIAGRAM_PATH}" ABSOLUTE)

        set(OUTPUT_HEADER "${FSM_ARG_OUTPUT_DIR}/${DIAGRAM_NAME}_fsm.hpp")

        set(CLI_ARGS -i "${DIAGRAM_ABS}" -o "${OUTPUT_HEADER}" --allow-diagram-codegen)

        if(FSM_ARG_TARGET_LANG)
            list(APPEND CLI_ARGS --target "${FSM_ARG_TARGET_LANG}")
        endif()

        if(FSM_ARG_NAME)
            list(APPEND CLI_ARGS --name "${FSM_ARG_NAME}")
        endif()

        if(FSM_ARG_STANDARD)
            list(APPEND CLI_ARGS --std "${FSM_ARG_STANDARD}")
        endif()

        if(FSM_ARG_NAMESPACE)
            list(APPEND CLI_ARGS --namespace "${FSM_ARG_NAMESPACE}")
        endif()

        if(FSM_ARG_FORMAT)
            list(APPEND CLI_ARGS --format "${FSM_ARG_FORMAT}")
        endif()

        if(FSM_ARG_STANDALONE)
            list(APPEND CLI_ARGS --standalone)
        elseif(FSM_ARG_MODULAR)
            list(APPEND CLI_ARGS --modular)
        endif()

        if(FSM_ARG_NO_THREAD_SAFE)
            list(APPEND CLI_ARGS --no-thread-safe)
        endif()

        if(FSM_ARG_NO_STUBS)
            list(APPEND CLI_ARGS --no-stubs)
        endif()

        add_custom_command(
            OUTPUT "${OUTPUT_HEADER}"
            COMMAND ${FSMC_EXE} ${CLI_ARGS}
            DEPENDS "${DIAGRAM_ABS}" ${FSMC_DEP}
            COMMENT "Generating C++ FSM header from ${DIAGRAM_ABS}"
            VERBATIM
        )

        list(APPEND GENERATED_HEADERS "${OUTPUT_HEADER}")
    endforeach()

    target_sources(${TARGET_NAME} PRIVATE ${GENERATED_HEADERS})
    target_include_directories(${TARGET_NAME} PRIVATE "${FSM_ARG_OUTPUT_DIR}")
endfunction()
