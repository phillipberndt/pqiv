project(
  'pqiv',
  'c',
  meson_version: '>= 1.3.0',
  version: '2.13.3',
  default_options: ['warning_level=3', 'c_std=gnu99'],
)

cc = meson.get_compiler('c')

if host_machine.system() == 'windows'
  gio_platform = dependency('gio-windows-2.0')
else
  gio_platform = dependency('gio-unix-2.0')
endif

dependencies = [
  dependency('glib-2.0', version: '>=2.8'),
  dependency('gtk+-3.0'),
  dependency('gdk-3.0'),
  dependency('cairo', version: '>=1.6'),
  dependency('pango', version: '>=1.10'),
  dependency('gio-2.0'),
  gio_platform,
  dependency('X11'),
  cc.find_library('m', required: false),
]

lib_headers = include_directories('lib')
lib_sources = [
  'lib/strnatcmp.c',
  'lib/bostree.c',
  'lib/filebuffer.c',
  'lib/config_parser.c',
  'lib/thumbnailcache.c',
]

exe = executable(
  'pqiv',
  'pqiv.c',
  lib_sources,
  include_directories: lib_headers,
  dependencies: dependencies,
  install: true,
)

test('basic', exe)