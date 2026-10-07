project(
    'cppgir_example',
    ['c', 'cpp'],
    version: '2.0.0',
    meson_version: '>= 0.61',
    default_options: ['warning_level=2', 'cpp_std=c++17'],
)

# following example uses the Gio-2.0 gir
# will generate code for that

# locate cppgir, either in system or subproject
# if subproject, arrange to embed ignore data
# (since it will run un-installed, so data can not be found in installed location)
gi = dependency(
    'cppgir',
    required: true,
    version: '>=2.0.0',
    fallback: 'cppgir',
    default_options: ['build_embed_ignore=true'],
)
cppgir = find_program('cppgir', required: true)

generated_src = custom_target(
    'src',
    # actually it generates all this (and more)
    # output : ['gio.cpp', 'gio.hpp', 'glib.cpp', 'glib.hpp', 'gobject.cpp', 'gobject.hpp'],
    # but the example below will inline all of the above, so no source files are used
    # (though it is recommended for a real case, rather than a full inline)
    output: ['gio.hpp'],
    console: true,
    # output-top creates extra files in top-level (as oppposed to subdir)
    # since "output" paths above "must not contain a path segment" (says meson, sigh)
    command: [cppgir, '--output-top', '--output', '@OUTDIR@', 'Gio-2.0'],
)

# need the code and libs and especially GIRs
gobject = dependency('gobject-2.0', required: true)
gio = dependency('gio-2.0', required: true)
# the Gio GIR data also covers this, so generated code references it
gio_unix = dependency('gio-unix-2.0', required: true)

deps = [gi, gobject, gio, gio_unix]

# (actually) needs no generated code, but it needs basic libs
ex_gobject = executable(
    'ext-gobject',
    ['src/ext-gobject.cpp'],
    dependencies: deps,
)

# (really) needs generated code
ex_gio = executable(
    'ext-gio',
    ['src/ext-gio.cpp', generated_src],
    dependencies: deps,
)
