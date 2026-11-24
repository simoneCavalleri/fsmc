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
    set(options STANDALONE MODULAR NO_THREAD_SAFE NO_STUBS ALLOW_DIAGRAM_CODEGEN FREESTANDING EMIT_C_ABI EMIT_MANIFEST)
    set(oneValueArgs NAME NAMESPACE OUTPUT_DIR FORMAT TARGET TARGET_LANG RUNTIME_HEADER)
    set(multiValueArgs DIAGRAMS)

    cmake_parse_arguments(FSM_ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(FSM_ARG_TARGET AND NOT FSM_ARG_TARGET_LANG)
        set(FSM_ARG_TARGET_LANG "${FSM_ARG_TARGET}")
    endif()

    if(NOT FSM_ARG_DIAGRAMS)
        message(FATAL_ERROR "fsmc_target_sources: No DIAGRAMS specified for target ${TARGET_NAME}")
    endif()

    if(NOT FSM_ARG_OUTPUT_DIR)
        set(FSM_ARG_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_${TARGET_NAME}")
    endif()

    file(MAKE_DIRECTORY "${FSM_ARG_OUTPUT_DIR}")

    # Determine fsmc executable location:
    # 1. Explicit host compiler path for cross-compilation (FSMC_HOST_EXECUTABLE)
    # 2. In-tree target (fsmc) or imported package target (fsmc::fsmc)
    # 3. System PATH fallback
    if(DEFINED FSMC_HOST_EXECUTABLE AND NOT "${FSMC_HOST_EXECUTABLE}" STREQUAL "")
        set(FSMC_EXE "${FSMC_HOST_EXECUTABLE}")
        set(FSMC_DEP "${FSMC_HOST_EXECUTABLE}")
    elseif(TARGET fsmc)
        set(FSMC_EXE "$<TARGET_FILE:fsmc>")
        set(FSMC_DEP fsmc)
    elseif(TARGET fsmc::fsmc)
        set(FSMC_EXE "$<TARGET_FILE:fsmc::fsmc>")
        set(FSMC_DEP fsmc::fsmc)
    else()
        find_program(FSMC_EXE NAMES fsmc REQUIRED)
        set(FSMC_DEP "${FSMC_EXE}")
    endif()

    set(GENERATED_HEADERS "")

    foreach(DIAGRAM_PATH IN LISTS FSM_ARG_DIAGRAMS)
        get_filename_component(DIAGRAM_NAME "${DIAGRAM_PATH}" NAME_WE)
        get_filename_component(DIAGRAM_ABS "${DIAGRAM_PATH}" ABSOLUTE)

        set(IS_C99_TARGET FALSE)
        if(FSM_ARG_TARGET_LANG STREQUAL "c99" OR FSM_ARG_TARGET_LANG STREQUAL "c")
            set(IS_C99_TARGET TRUE)
        endif()

        if(IS_C99_TARGET)
            set(OUTPUT_HEADER "${FSM_ARG_OUTPUT_DIR}/${DIAGRAM_NAME}.h")
        else()
            set(OUTPUT_HEADER "${FSM_ARG_OUTPUT_DIR}/${DIAGRAM_NAME}_fsm.hpp")
        endif()

        set(CLI_ARGS -i "${DIAGRAM_ABS}" -o "${OUTPUT_HEADER}")

        if(FSM_ARG_ALLOW_DIAGRAM_CODEGEN)
            list(APPEND CLI_ARGS --allow-diagram-codegen)
        endif()

        if(FSM_ARG_TARGET_LANG)
            list(APPEND CLI_ARGS --target "${FSM_ARG_TARGET_LANG}")
        endif()

        if(FSM_ARG_NAME)
            list(APPEND CLI_ARGS --name "${FSM_ARG_NAME}")
        endif()

        if(FSM_ARG_NAMESPACE)
            list(APPEND CLI_ARGS --namespace "${FSM_ARG_NAMESPACE}")
        endif()

        if(FSM_ARG_FORMAT)
            list(APPEND CLI_ARGS --format "${FSM_ARG_FORMAT}")
        endif()

        if(NOT IS_C99_TARGET)
            if(FSM_ARG_STANDALONE)
                list(APPEND CLI_ARGS --standalone)
            elseif(FSM_ARG_MODULAR)
                list(APPEND CLI_ARGS --modular)
            endif()

            if(FSM_ARG_RUNTIME_HEADER)
                list(APPEND CLI_ARGS --runtime-header "${FSM_ARG_RUNTIME_HEADER}")
            endif()

            if(FSM_ARG_NO_THREAD_SAFE)
                list(APPEND CLI_ARGS --no-thread-safe)
            endif()

            if(FSM_ARG_FREESTANDING)
                list(APPEND CLI_ARGS --freestanding)
            endif()

            if(FSM_ARG_NO_STUBS)
                list(APPEND CLI_ARGS --no-stubs)
            endif()

            if(FSM_ARG_EMIT_C_ABI)
                list(APPEND CLI_ARGS --emit-c-abi)
            endif()
        endif()

        if(FSM_ARG_EMIT_MANIFEST)
            list(APPEND CLI_ARGS --emit-manifest)
        endif()

        set(CMD_OUTPUTS "${OUTPUT_HEADER}")
        if(IS_C99_TARGET)
            set(OUTPUT_C_SOURCE "${FSM_ARG_OUTPUT_DIR}/${DIAGRAM_NAME}.c")
            list(APPEND CMD_OUTPUTS "${OUTPUT_C_SOURCE}")
        else()
            if(NOT FSM_ARG_STANDALONE)
                set(OUTPUT_PORTS_HEADER "${FSM_ARG_OUTPUT_DIR}/${DIAGRAM_NAME}_ports.hpp")
                list(APPEND CMD_OUTPUTS "${OUTPUT_PORTS_HEADER}")
            endif()
            if(FSM_ARG_EMIT_C_ABI)
                set(OUTPUT_C_HEADER "${FSM_ARG_OUTPUT_DIR}/${DIAGRAM_NAME}_c.h")
                set(OUTPUT_C_SOURCE "${FSM_ARG_OUTPUT_DIR}/${DIAGRAM_NAME}_c.cpp")
                list(APPEND CMD_OUTPUTS "${OUTPUT_C_HEADER}" "${OUTPUT_C_SOURCE}")
            endif()
        endif()

        if(IS_C99_TARGET)
            set(GEN_COMMENT "Generating Pure-C99 FSM artifacts from ${DIAGRAM_ABS}")
        else()
            set(GEN_COMMENT "Generating C++17 FSM artifacts from ${DIAGRAM_ABS}")
        endif()

        add_custom_command(
            OUTPUT ${CMD_OUTPUTS}
            COMMAND ${FSMC_EXE} ${CLI_ARGS}
            DEPENDS "${DIAGRAM_ABS}" ${FSMC_DEP}
            COMMENT "${GEN_COMMENT}"
            VERBATIM
        )
        list(APPEND GENERATED_HEADERS ${CMD_OUTPUTS})
    endforeach()

    target_sources(${TARGET_NAME} PRIVATE ${GENERATED_HEADERS})
    target_include_directories(${TARGET_NAME} PRIVATE "${FSM_ARG_OUTPUT_DIR}")
endfunction()
