project(
  'bolt',
  ['c'],
  version: '0.1.3',
)

add_project_arguments('-Wno-switch', language: 'c')
add_project_arguments('-Wno-unused-variable', language: 'c')
add_project_arguments('-Wno-format', language: 'c')

default_library = get_option('default_library')
build_shared = default_library == 'both' or default_library == 'shared'
build_static = default_library == 'both' or default_library == 'static'
if build_shared and target_machine.system() == 'Windows'
  add_project_arguments('-DBOLT_SHARED_LIBRARY', language: 'c')
  add_project_arguments('-DBOLT_EXPORT_SHARED', language: 'c')
endif

subdir('bolt')
subdir('bolt-cli')
