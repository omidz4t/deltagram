import os

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.build import can_run


class CppGirExampleConan(ConanFile):
    version = "2.0.0"
    name = "cppgir-example"
    description = "cppgir example"
    license = "MIT Software License"
    url = "https://gitlab.com/mnauw/cppgir.git"
    exports_sources = "src/*", "CMakeLists.txt"
    settings = "os", "compiler", "build_type", "arch"
    build_policy = "missing"
    author = "Mark Nauwelaerts"

    def requirements(self):
        req = self.tested_reference_str
        if req is None:
            req = "cppgir/" + self.version
        self.requires(req)
        # also requires GLib
        # however, no GIRs in conan binary package
        # which nowadays requires a bootstrap dance with gobject-introspection
        # so rely on a distro package instead
        # self.requires("glib/2.78.3")

    def build_requirements(self):
        self.tool_requires("cppgir/<host_version>",
                           options={'header_only': False})

    def layout(self):
        cmake_layout(self, build_folder="build.conan")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            cmd = os.path.join(self.cpp.build.bindir, "ext-gobject")
            self.run(cmd, env="conanrun")
