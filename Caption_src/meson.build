caption_sources = [
  'ARIB8CharDecode.cpp',
  'Caption.cpp',
  'CaptionMain.cpp',
]

caption_inc = include_directories('.')
caption_include_dirs = [
  caption_inc,
  include_directories('../../common'),
]

# プラットフォーム固有の定義
caption_cpp_args = cpp_args
if host_machine.system() != 'windows'
  # Linuxビルド時にはプリプロセッサでヘッダーファイルを含める前にpltform_compat.hを含めるよう指示
  caption_cpp_args += ['-include', meson.current_source_dir() + '/platform_compat.h']
endif

caption_lib = static_library('caption',
  caption_sources,
  include_directories : caption_include_dirs,
  cpp_args : caption_cpp_args,
  pic : true,
  install : false,
)

caption_dep = declare_dependency(
  link_with : caption_lib,
  include_directories : caption_include_dirs,
) 