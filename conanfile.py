from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout

class FsmcConan(ConanFile):
    name = "fsmc"
    version = "0.14.0"
    description = "Universal Finite State Machine Compiler, Optimization Infrastructure, Formal Verification & Zero-Overhead C++17 / C99 ABI Engine"

    license = "MIT"
    url = "https://github.com/simoneCavalleri/fsmc"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "with_zlib": [True, False],
    }
    default_options = {
        "with_zlib": True,
    }
    generators = "CMakeDeps"
    exports_sources = (
        "CMakeLists.txt",
        "include/*",
        "src/*",
        "cmake/*",
        "tools/*",
    )

    def requirements(self):
        if self.options.with_zlib:
            self.requires("zlib/[>=1.2.11 <2]")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["FSMC_ENABLE_TESTING"] = False
        tc.variables["FSMC_ENABLE_EXAMPLES"] = False
        tc.variables["FSMC_ENABLE_BENCHMARKS"] = False
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "fsmc")
        self.cpp_info.set_property("cmake_target_name", "fsmc::compiler")

        # Header-only runtime component
        self.cpp_info.components["runtime"].set_property("cmake_target_name", "fsmc::runtime")
        self.cpp_info.components["runtime"].includedirs = ["include"]
        self.cpp_info.components["runtime"].libdirs = []
        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.components["runtime"].system_libs = ["pthread"]

        # Core IR & diagnostics
        self.cpp_info.components["core"].set_property("cmake_target_name", "fsmc::core")
        self.cpp_info.components["core"].libs = ["fsmc_core"]
        self.cpp_info.components["core"].requires = ["runtime"]

        # Frontend parsers
        self.cpp_info.components["frontend"].set_property("cmake_target_name", "fsmc::frontend")
        self.cpp_info.components["frontend"].libs = ["fsmc_frontend"]
        self.cpp_info.components["frontend"].requires = ["core"]
        if self.options.with_zlib:
            self.cpp_info.components["frontend"].requires.append("zlib::zlib")

        # Middle-end passes & analysis
        self.cpp_info.components["middleend"].set_property("cmake_target_name", "fsmc::middleend")
        self.cpp_info.components["middleend"].libs = ["fsmc_middleend"]
        self.cpp_info.components["middleend"].requires = ["core"]
        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.components["middleend"].system_libs = ["dl"]

        # Backend codegen & emitters
        self.cpp_info.components["backend"].set_property("cmake_target_name", "fsmc::backend")
        self.cpp_info.components["backend"].libs = ["fsmc_backend"]
        self.cpp_info.components["backend"].requires = ["core"]

        # Aggregate compiler component
        self.cpp_info.components["compiler"].set_property("cmake_target_name", "fsmc::compiler")
        self.cpp_info.components["compiler"].requires = ["core", "frontend", "middleend", "backend"]
        self.cpp_info.components["compiler"].bindirs = ["bin"]
