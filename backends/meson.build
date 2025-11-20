backends = []
backend_dependencies = []
backend_sources = []

gdkpixbuf = dependency('gdk-pixbuf-2.0', version: '>= 2.2', required: get_option('gdkpixbuf'))
if gdkpixbuf.found()
    backends += ['gdkpixbuf']
    backend_dependencies += gdkpixbuf
    backend_sources += 'backends/gdkpixbuf.c'
endif

poppler = dependency('poppler-glib', required: get_option('poppler'))
if poppler.found()
    backends +=['poppler']
    backend_dependencies += poppler
    backend_sources += 'backends/poppler.c'
endif

spectre = dependency('libspectre', required: get_option('spectre'))
if spectre.found()
    backends +=['spectre']
    backend_dependencies += spectre
    backend_sources += 'backends/spectre.c'
endif

archive = dependency('libarchive', required: get_option('archive'))
if archive.found()
  backends += ['archive']
  backend_dependencies += archive
  backend_sources += 'backends/archive.c'
endif

#archive_cbx - think how to make it elegantly

prog_python = find_program('python3')

generated_sources = custom_target(
    'initializer.c',
    input: 'generate_initializer.py',
    output: 'initializer.c',
    command: [prog_python, '@INPUT@', '@OUTPUT@', backends],
)