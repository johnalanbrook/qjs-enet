project('enet_project', 'c')

quickjs = dependency('quickjs')
enet = dependency('enet')

qjs_enet_lib = library('enet',
  sources: 'qjs_enet.c',
  dependencies: [enet, quickjs],
  c_shared_args: '-DJS_SHARED_LIBRARY',
)

qjs_enet_dep = declare_dependency(
  link_with: qjs_enet_lib,
)
