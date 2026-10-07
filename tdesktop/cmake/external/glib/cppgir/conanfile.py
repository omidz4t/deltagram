import os

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout


class CppGirConan(ConanFile):
    version = "2.0.0"
    name = "cppgir"
    description = "gobject-introspection C++ binding generator"
    license = "MIT Software License"
    url = "https://gitlab.com/mnauw/cppgir.git"
    exports_sources = "data/*", "docs/*", "expected-lite/*", \
        "gi/*", "override/*", "tools/*", "test/*", \
        "examples/*", "!examples/external/*", \
        "CMakeLists.txt", "cmake/*", "LICENSE", "README.md"
    # ignore data depends on OS
    settings = "os", "compiler", "build_type", "arch"
    build_policy = "missing"
    author = "Mark Nauwelaerts"
    test_package_folder = "examples/external"

    options = {"header_only": [True, False]}

    # default host dependency case
    default_options = {"header_only": True}

    def requirements(self):
        if not self.options.header_only:
            self.requires("boost/[>=1.58]", options={'header_only': True})
            self.requires("fmt/[>=8.1.1]")

    def layout(self):
        cmake_layout(self, build_folder="build.conan")
        # adjust for editable
        self.cpp.source.includedirs = ["."]
        # self.cpp.build.bindir = "."

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["BUILD_EXAMPLES"] = False
        tc.variables["BUILD_DOC"] = False
        # cmake --install relocation does not play well with ignore paths
        # so have those compiled in
        tc.variables["BUILD_EMBED_IGNORE"] = True
        tc.variables["BUILD_TOOLS"] = not self.options.header_only
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        if not self.conf.get("tools.build:skip_test", default=False):
            test_folder = os.path.join(".")
            # test depends on GLib, which may not be present
            for t in ["gi-test", "gi-test-17"]:
                test = os.path.join(test_folder, t)
                if os.path.exists(test):
                    self.run(test)

    def package(self):
        """Run CMake install"""
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.includedirs = ["include/cppgir"]

    def package_id(self):
        # options access not allowed here, unfortunately
        if False and self.options.header_only:
            self.info.clear()
