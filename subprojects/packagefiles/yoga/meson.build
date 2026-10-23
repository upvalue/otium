project('yoga', 'cpp', version: '3.2.1', default_options: ['cpp_std=c++20'])

yoga = static_library('yoga',
  'yoga/YGConfig.cpp', 'yoga/YGEnums.cpp', 'yoga/YGNode.cpp', 'yoga/YGNodeLayout.cpp',
  'yoga/YGNodeStyle.cpp', 'yoga/YGPixelGrid.cpp', 'yoga/YGValue.cpp',
  'yoga/algorithm/AbsoluteLayout.cpp', 'yoga/algorithm/Baseline.cpp', 'yoga/algorithm/Cache.cpp',
  'yoga/algorithm/CalculateLayout.cpp', 'yoga/algorithm/FlexLine.cpp', 'yoga/algorithm/PixelGrid.cpp',
  'yoga/config/Config.cpp', 'yoga/debug/AssertFatal.cpp', 'yoga/debug/Log.cpp',
  'yoga/event/event.cpp', 'yoga/node/LayoutResults.cpp', 'yoga/node/Node.cpp',
  cpp_args: ['-fno-omit-frame-pointer', '-fexceptions', '-fno-rtti', '-fvisibility=hidden'],
  include_directories: include_directories('.'),
  pic: true,
  override_options: ['warning_level=1', 'werror=false'])

yoga_dep = declare_dependency(link_with: yoga, include_directories: include_directories('.'))
