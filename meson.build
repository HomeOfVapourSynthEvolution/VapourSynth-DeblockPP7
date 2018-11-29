project('DeblockPP7', 'cpp',
  default_options : ['buildtype=release', 'b_ndebug=if-release', 'cpp_std=c++14'],
  meson_version : '>=0.48.0',
  version : '4'
)

sources = [
  'DeblockPP7/DeblockPP7.cpp',
  'DeblockPP7/DeblockPP7.hpp',
  'DeblockPP7/vectorclass/instrset.h',
  'DeblockPP7/vectorclass/instrset_detect.cpp'
]

libs = []

vapoursynth_dep = dependency('vapoursynth').partial_dependency(compile_args : true, includes : true)

add_project_arguments('-ffast-math', language : 'cpp')

if host_machine.cpu_family().startswith('x86')
  sources += [
    'DeblockPP7/DeblockPP7_SSE2.cpp',
    'DeblockPP7/vectorclass/vectorclass.h',
    'DeblockPP7/vectorclass/vectorf128.h',
    'DeblockPP7/vectorclass/vectori128.h'
  ]

  add_project_arguments('-DVS_TARGET_CPU_X86', '-mfpmath=sse', '-msse2', language : 'cpp')

  libs += static_library('sse4', 'DeblockPP7/DeblockPP7_SSE4.cpp',
    dependencies : vapoursynth_dep,
    cpp_args : '-msse4.1',
    gnu_symbol_visibility : 'hidden'
  )
endif

shared_module('deblockpp7', sources,
  dependencies : vapoursynth_dep,
  link_with : libs,
  install : true,
  install_dir : join_paths(vapoursynth_dep.get_pkgconfig_variable('libdir'), 'vapoursynth'),
  gnu_symbol_visibility : 'hidden'
)
