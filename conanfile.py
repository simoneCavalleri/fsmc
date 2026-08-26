from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout

class FsmcConan(ConanFile):
    name = "fsmc"
    version = "0.1.0"
    description = "Universal Finite State Machine Compiler, Optimization Infrastructure, Formal Verification & Zero-Overhead C++17/C++20 Engine"
    license = "MIT"
    url = "https://github.com/simoneCavalleri/fsmc"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"
    exports_sources = "CMakeLists.txt", "include/*", "src/*", "cmake/*", "examples/*", "tests/*"

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = []
        self.cpp_info.bindirs = ["bin"]
        self.cpp_info.includedirs = ["include"]
