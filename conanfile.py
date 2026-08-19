from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout

class FsmcConan(ConanFile):
    name = "fsmc"
    version = "1.0.0"
    description = "A zero-overhead OMG UML 2.5 & SysML v2 Finite State Machine compiler for C++17/20 (SysML v2, PlantUML, Mermaid)"
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
