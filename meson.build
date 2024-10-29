project('enet_project', 'c')

quickjs = dependency('quickjs', static:true)
enet = dependency('enet', static:true)

qjs_enet_lib = library('enet',
  sources: ['qjs_enet.c'],
  dependencies: [quickjs, enet]
)

qjs_enet_dep = declare_dependency(
  link_with: enet_lib,
)
