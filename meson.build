project(
  'draw-engine',
  'c',
  meson_version : '>= 1.3.0',
  version : '0.1',
  default_options : ['warning_level=3'],
)


lib_deps = [
]

lib_srcs = [
  'core/app.c'
]

lib = shared_library(
  'libengine',
  lib_srcs,
  dependencies : lib_deps,
  install : true,
)

exe_deps = [
]

exe = executable(
  'test-app',
  'test_app.c',
  dependencies : exe_deps,
  link_with : [lib],
  install : true,
)

test('basic', exe)
