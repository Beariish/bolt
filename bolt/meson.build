bolt_includes = include_directories(['.'])
boltstd_includes = include_directories(['boltstd'])

bolt_sources = [
  'bolt.c',
  'bt_buffer.c',
  'bt_compiler.c',
  'bt_debug.c',
  'bt_embedding.c',
  'bt_gc.c',
  'bt_object.c',
  'bt_parser.c',
  'bt_prelude.c',
  'bt_tokenizer.c',
  'bt_type.c',
  'bt_userdata.c',
  'bt_value.c',
]

boltstd_sources = [
  'boltstd/picomatch/picomatch.c',
  'boltstd/boltstd.c',
  'boltstd/boltstd_arrays.c',
  'boltstd/boltstd_core.c',
  'boltstd/boltstd_math.c',
  'boltstd/boltstd_meta.c',
  'boltstd/boltstd_strings.c',
  'boltstd/boltstd_tables.c',
  'boltstd/boltstd_io.c',
  'boltstd/boltstd_regex.c',
]

libbolt = library('bolt', bolt_sources, install: true)

bolt = declare_dependency(
  link_with: libbolt,
  include_directories: bolt_includes,
)

libboltstd = library('boltstd',
  boltstd_sources,
  dependencies: [bolt],
  install: true,
)

boltstd = declare_dependency(
  link_with: [libboltstd],
  include_directories: boltstd_includes,
)

install_headers([
  'bolt.h',
  'bt_buffer.h',
  'bt_compiler.h',
  'bt_config.h',
  'bt_context.h',
  'bt_debug.h',
  'bt_embedding.h',
  'bt_gc.h',
  'bt_object.h',
  'bt_op.h',
  'bt_parser.h',
  'bt_prelude.h',
  'bt_tokenizer.h',
  'bt_type.h',
  'bt_userdata.h',
  'bt_value.h',
])

install_headers([
  'boltstd/picomatch/picomatch.h',
  'boltstd/boltstd.h',
  'boltstd/boltstd_arrays.h',
  'boltstd/boltstd_core.h',
  'boltstd/boltstd_math.h',
  'boltstd/boltstd_meta.h',
  'boltstd/boltstd_strings.h',
  'boltstd/boltstd_tables.h',
  'boltstd/boltstd_io.h',
  'boltstd/boltstd_regex.h',
])

pkg = import('pkgconfig')
pkg.generate(libbolt, description: 'Bolt embedded programming language')
pkg.generate(libboltstd, description: 'Bolt standard library')
