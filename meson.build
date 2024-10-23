project('qjs-enet', 'c')

inc = include_directories('.')

quickjs = dependency('quickjs', fallback:['quickjs', 'quickjs_dep'])
enet = dependency('enet', fallback:['enet', 'enet_dep'])

libtype = get_option('default_library')
if libtype == 'static'
  add_project_arguments('-DJS_SHARED_LIBRARY', language:'c')
endif

qjs_enet_lib = library('qjs-enet',
  sources: 'qjs_enet.c',
  include_directories: [inc],
  dependencies: [quickjs, enet],
)

qjs_enet_dep = declare_dependency(
  link_with: qjs_enet_lib,
  include_directories : inc
)
