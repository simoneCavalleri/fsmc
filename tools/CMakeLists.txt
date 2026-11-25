# ==============================================================================
# CLI Compiler and IR Toolchain Targets
# ==============================================================================

include(GNUInstallDirs)

# Compiled driver library shared by CLI executables and integration tests
add_library(fsmc_driver_lib STATIC
    fsmc/fsmc_driver.cpp
    fsmc/driver_codegen.cpp
    fsmc/driver_verify.cpp
    fsm-opt/opt_driver.cpp
)
target_link_libraries(fsmc_driver_lib PUBLIC fsmc_compiler fsmc_runtime PRIVATE $<BUILD_INTERFACE:fsmc_project_options>)
if(NOT EMSCRIPTEN)
    target_link_libraries(fsmc_driver_lib PUBLIC ${CMAKE_DL_LIBS})
endif()
target_include_directories(fsmc_driver_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)
target_compile_features(fsmc_driver_lib PUBLIC cxx_std_20)

# Primary compiler driver (fsmc)
add_executable(fsmc fsmc/main.cpp)
target_link_libraries(fsmc PRIVATE fsmc_driver_lib $<BUILD_INTERFACE:fsmc_project_options>)
target_compile_features(fsmc PRIVATE cxx_std_20)
if(NOT EMSCRIPTEN)
    set_target_properties(fsmc PROPERTIES ENABLE_EXPORTS ON)
endif()

# Formal IR Optimizer and Linter tool (fsm-opt)
add_executable(fsm-opt fsm-opt/main.cpp)
target_link_libraries(fsm-opt PRIVATE fsmc_driver_lib $<BUILD_INTERFACE:fsmc_project_options>)
target_compile_features(fsm-opt PRIVATE cxx_std_20)
if(NOT EMSCRIPTEN)
    set_target_properties(fsm-opt PROPERTIES ENABLE_EXPORTS ON)
endif()


# Install and export targets
install(TARGETS fsmc fsm-opt
    EXPORT fsmcTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
