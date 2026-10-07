# cppgir

`cppgir` is a [GObject-Introspection](https://wiki.gnome.org/Projects/GObjectIntrospection)
C++ binding wrapper generator.  That is, it processes `.gir` files derived
from GObject-Introspection annotations into a set of C++ files defining
suitable namespaces, classes and other types that together from a C++ binding.
In this way, the plain C libraries and objects become available as native
objects along with (RAII) managed resource handling.  The generated code
only requires a C++14 compiler and library (as well as obviously the underlying
C headers and libraries that are being wrapped).


## Installation

The code generator can be built using [CMake](https://cmake.org) or
[meson](https://mesonbuild.com/) and requires
Boost and either [fmtlib](http://fmtlib.net/) or a C++20 std library with format
support (e.g. gcc 13+).  While it obviously depends on distribution,
these may be typically be obtained by installing following packages;

    libfmt-dev libboost-dev

If it is required to compile the manual page ronn is required to be installed

The following step will provide an additional dependency as a submodule;

    git submodule update --init

With all that in place, the usual steps apply, so e.g.

    mkdir build
    cd build
    cmake ..
    cmake --build .
    cmake --install .

or alternatively

    mkdir build
    cd build
    meson setup . ..
    meson compile
    meson install

The installed components consist of:

* the code generator executable
* (if not embedded by build option) default ignore files
* common headers required by the generated code and default overrides

The latter is needed when using the generated code, the former only
at generation time.

In case of CMake build, it is also possible to build some examples whose
sources are also installed along with some documentation.
As such, the meson system only builds and install the (most) relevant parts,
as may be useful when used as a subproject.

For either build system, an [example](examples/external) shows how `cppgir`
can be used in a project.

## Release numbering and API stability

The generated code for an even-numbered major version should be (compile) API
stable.  If the major version turns odd, then a cycle/series of incompatible
changes may occur, which after actual practice and time can then become
the next even stable version.


## Running

All details can be found in the [manpage](docs/cppgir.md), but a simple
example wrapping [GStreamer](https://gstreamer.freedesktop.org/) libraries is
as follows:

    generate --output /tmp/gi GStreamer-1.0

The above (obviously) assumes that suitable `.gir` files are present in
the usual location, as typically installed by a `-dev` package.  It is
also assumed the generator has been installed (so "running installed").
Running the generator uninstalled is also possible, but then (if not embedded) the
default ignore files in `data` directory have to explicitly specified as well
(using either option or environment variable as specified in manual).

That's it, all wrapping code is now available in `/tmp/gi` (in a number of
subdirectories). It only requires a C++14 compiler and library and can be
included and compiled into the target library or executable. The latter then
has no additional dependencies other than the plain C libraries (gstreamer and
lower level ones in this case).


## Compilation

The wrapping binding code presents an API along the lines of any other binding,
which also means it maps pretty straight onto the original library API.
So all API is simply found where expected.  To use it fully inline,
the following include snippet suffices:

    #define GI_INLINE 1
    #include <gst/gst.hpp>

Of course, the include path must have been setup properly to contain the output
root of the generated code (i.e. `/tmp/gi` in previous example). It must also
contain the proper path for header code supplied by this repo (using e.g. a
pkgconfig fragment if it has been installed, or an IMPORT target or by other
means).

See the [manpage](docs/cppgir.md) for additional essential remarks on the API
of the generated code or typical non-inline use of the code.


## Examples

The examples (in the so-named directory) illustrate and cover various practical
aspects and use of the generated API.  While they are all simple and trivial,
they do illustrate use in various domains such as `Gio`, `Gst` and `Gtk`.

For example, when it comes to traditional C `libgio`, there is usually a choice
between a synchronous (blocking) call and a corresponding asynchronous
(non-blocking) call. The latter is then typically used to establish a chain of
completion handlers. In other settings and languages, it has become customary
to employ async/await operations/keywords. That way, the flow of code remains
linear as in the blocking case. However, some form of callback chain is
typically present behind the (implementation) scenes and a more practical
consequence is that async/await keywords need to be sprinkled in quite some
places (down to call stack depth). That, in turn, is then intrusive/disruptive
in its own way.

Alternatively, one can use so-called stackful co-routines or fibers to maintain
stack context when execution has to be suspended (e.g. awaiting I/O, or some
condition). An example implementation is provided by [Boost
Fiber](https://www.boost.org/doc/libs/latest/libs/fiber/doc/html/index.html)
which caters for (cooperative) switching between multiple fibers on a single
thread. This is viable in C++ since such code should be exception-safe anyway
and as such should not be taken by surprise upon code execution not making it
all the way through a code sequence. After all, that might happen at any time
upon exception, as in the particular case of fiber context destruction in stead
of regular fiber termination. This approach also allows using legacy code in
multiple fibers on a single thread, since no additional synchronization is
required, and no special support (or keyword sprinkling) is needed along the
callstack (down to point of suspension). To come down to it, the async Gio
example provides a `GMainContext` based `Fiber` scheduler to integrate and
support using Fiber in a standard GLib mainloop setup. In particular, that
allows turning `Gio`'s async calls into (apparent) blocking calls when run on a
fiber, which then leads to a concise sequential code flow.


## Features

A few simple but nice to have features of the generated code are:

* well structured and easily understood
* it can be used either fully inline or have implementation code compiled
  into one or more object files for subsequent linking
* it allows for extensions/overrides (similar to e.g. PyGObject)
  supplied by separate files (so not in the generated ones),
  either default ones supplied along with this repo or added custom by
  the project using the generated code

Another feauture might be handy even if one is not interested in generated
C++ code.  As the `.gir` files as processed, a number of consistency checks
are performed and warned about.  In that way, the generator also acts somewhat
as annotation validator, and can typically spot some missing `(out)` or
`(array)` (and related) annotations.

Some other features have been implicitly mentioned above, but are perhaps best
high-lighted by addressing how it differs from [gktmm](https://www.gtkmm.org)
(and the related question; why another C++ interface). To this end, first
note or recall that gtkmm consists of many repositories (glibmm, gtkmm,
gstreamermm, ...), all of which typically also yield a distribution
package for library code and headers. So, when using e.g. gstreamermm, several
such packages are required, either for their headers at compile time or for the
libraries at runtime. In contrast, only 1 repo is needed here, and using the
generated code then incurs no additional (runtime) dependencies other than the
(unavoidable) C libraries.

The code in gtkmm's (and friends') libraries can be considered as hand-crafted.
Well, it is produced based on templates, but those are manually maintained. It
does not use or consider the GObject-Introspection annotations in any way (not
in the least because it predates that system). So, as a C library and API
evolves, the corresponding gtkmm has to be manually synchronized. That is, if
there is at least a corresponding gtkmm one. For a not-so-well-known-or-popular
one there may not be, and definitely not for a custom developed one. On the
other hand, whenever (minimal) annotations are available, you are good to go
with any binding, whether PyGObject or the one provided here.

Since the gtkmm code is hand-crafted API from the ground up, that allows for
some nifty things (such as the signal approach mentioned further below). On the
other hand, sometimes it might be too nifty in that the C++ API does not quite
track or match the original one. Instead, it is then more of an alternative or
parallel one. For example, in GStreamer, there is (only) one (mini-object)
C-type of `GstEvent` (with a type field indicating the precise type of event).
So it is handled this way throughout the API and in bindings (e.g. PyGObject).
However, in gstreamermm it has been chosen to have many types (i.e. subclasses)
of `Gst::Event`. This may well be a natural C++ way (or a Python one if done
with Python classes), but all together it presents an API where things are not
quite where expected, and it is in that regard not a straight binding. In
contrast, the generated code here is as straight as can be, and functions to
call are right where the fingers expect them to be, whether in PyGObject or
here.

So, in the concept of "C++ binding API", gtkmm has emphasis on C++
up to the point of almost a parallel or independent API with quite some
trimming. The latter is illustrated by the separate
[libsigcplusplus](https://github.com/GNOME/libsigcplusplus) (with similar
notions in e.g. [boost signals2](https://github.com/boostorg/signals2)). Here,
however, the emphasis is on binding.

Of course, other than this straight binding, one is still free to use and bring
in any other lib, e.g. one of the aforementioned signal helper libs. However,
in most cases the "straight bindings" will suffice. Moreover, a few small
additional RAII helpers are provided that may be useful. See the examples for
an illustration on how they can be used.

## Limitations and Remarks

A compile-time binding is somewhat different than a typical more runtime script
language binding (e.g. Python).  While most of the annotated API is usually
well enough handled and covered for practical use, there are some limitations
and issues to consider.  Again, more details and further remarks are provided
in the [manpage](docs/cppgir.md)
