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

# Dependency for libwayland client library
wayland_dep = dependency('wayland-client')

# Import Meson's Wayland module for protocol scanning
wayland_mod = import('wayland')

# Find the xdg-shell protocol XML (from wayland-protocols)
xdg_xml = wayland_mod.find_protocol('xdg-shell')
# Generate C sources and headers from the protocol
xdg_sources = wayland_mod.scan_xml(xdg_xml)

disp_srcs = ['platform/display.c', 'platform/linux/window-wayland.c', 'platform/linux/shm.c']

# Build the executable
disp_exe = executable('wayland-app',
  disp_srcs,  # Your main source file
  xdg_sources,  # Generated protocol sources
  dependencies: wayland_dep,
  install: true
)

test('basic', exe)
