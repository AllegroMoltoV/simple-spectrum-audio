cmake_minimum_required(VERSION 3.21)

project(simple-spectrum-audio VERSION 1.0.0 LANGUAGES C CXX)

# Visage requires C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Link the C runtime statically so users need no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# The static libraries below are linked into the plugin modules. Only the symbols marked for export,
# such as clap_entry, GetPluginFactory, and the VST 3 module entry points, stay visible.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

option(SSP_SANITIZE "Build the tests with AddressSanitizer and UndefinedBehaviorSanitizer (GCC and Clang only)" OFF)

# Hardening applies to every compiled source, including the libraries that FetchContent adds below.
if(MSVC)
  add_compile_options(/GS /guard:cf)
else()
  add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2)
endif()

include(FetchContent)
# clap-wrapper v0.17.0. It downloads the CLAP and VST 3 SDKs at the versions it pins.
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_BUILD_AUV2 FALSE CACHE BOOL "" FORCE)
FetchContent_Declare(
  clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG 806bba0a5f792e3966b99c1561ab9952d3316d7c
)
FetchContent_MakeAvailable(clap-wrapper)

# Visage v1.0.0, pinned by commit and SHA-256. It draws the window with Direct3D 11 on Windows and Vulkan on Linux.
set(VISAGE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VISAGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# X11 only: a plug-in window cannot be embedded in a host window on Wayland, and the plug-in would need
# libwayland-client and libxkbcommon at run time.
set(VISAGE_WAYLAND OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
  # Visage uses the system's Vulkan headers when it finds them, and otherwise downloads them unpinned. Pin them here
  # and hand them to Visage, so every build uses the same headers and the archive carries their license.
  FetchContent_Declare(
    vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.341.tar.gz
    URL_HASH SHA256=8876aad926b2e72bdefddc34885472d5332e2d20aa3ed57313466f0bc982cf3f
    SOURCE_SUBDIR cmake-not-used
  )
  FetchContent_MakeAvailable(vulkan_headers)
  set(VISAGE_VULKAN_INCLUDE_DIR "${vulkan_headers_SOURCE_DIR}/include" CACHE PATH "" FORCE)
endif()
FetchContent_Declare(
  visage
  URL https://github.com/VitalAudio/visage/archive/5754ff8184cd0ba47a6f0a54a5f4a71974301036.tar.gz
  URL_HASH SHA256=aaa0268e7d50cd460c2a4c851337912215c0aa0e17ac8ba687ff0ba378829e7d
)
FetchContent_MakeAvailable(visage)

# pffft (marton78's maintained fork of Julien Pommier's PFFFT), pinned by commit and SHA-256. Only the
# single-precision FFT is compiled.
FetchContent_Declare(
  pffft
  URL https://github.com/marton78/pffft/archive/efc1dd2de6c3562fc464f905d37d9281f08a20dd.tar.gz
  URL_HASH SHA256=729190968a3f87257ff67aa3d55b77fff38df367d42f92e9e34f69cbece9f025
  # A directory without a CMakeLists.txt: FetchContent only downloads it, and the target below compiles it.
  SOURCE_SUBDIR cmake-not-used
)
FetchContent_MakeAvailable(pffft)
add_library(pffft STATIC "${pffft_SOURCE_DIR}/src/pffft.c" "${pffft_SOURCE_DIR}/src/pffft_common.c")
target_include_directories(pffft PUBLIC "${pffft_SOURCE_DIR}/include/pffft")
target_compile_definitions(pffft PUBLIC PFFFT_STATIC_DEFINE _USE_MATH_DEFINES)

set(PRODUCT_NAME "Simple Spectrum")

# The analyzer and the view do not depend on a window, so the tests use them directly.
add_library(spectrum-core STATIC src/spectrum-analyzer.cpp src/spectrum-view.cpp)
target_include_directories(spectrum-core PUBLIC src)
target_link_libraries(spectrum-core PUBLIC visage pffft PRIVATE VisageEmbeddedFonts)

add_library(simple-spectrum-impl STATIC src/plugin.cpp)
# clapwrapper/vst3.h, for the VST 3 factory information returned in src/plugin.cpp.
target_include_directories(simple-spectrum-impl PRIVATE "${clap-wrapper_SOURCE_DIR}/include")
target_compile_definitions(simple-spectrum-impl PRIVATE SIMPLE_SPECTRUM_VERSION="${PROJECT_VERSION}")
target_link_libraries(simple-spectrum-impl PUBLIC clap spectrum-core)

if(MSVC)
  target_compile_options(spectrum-core PRIVATE /W4 /permissive-)
  target_compile_options(simple-spectrum-impl PRIVATE /W4 /permissive-)
else()
  target_compile_options(spectrum-core PRIVATE -Wall -Wextra)
  target_compile_options(simple-spectrum-impl PRIVATE -Wall -Wextra)
endif()

make_clapfirst_plugins(
  TARGET_NAME simple-spectrum
  IMPL_TARGET simple-spectrum-impl
  OUTPUT_NAME "${PRODUCT_NAME}"
  ENTRY_SOURCE src/plugin-entry.cpp
  BUNDLE_IDENTIFIER "jp.allegromoltov.simple-spectrum"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  # VST 3 hosts and the registry both expect the bundle folder layout on Windows.
  WINDOWS_FOLDER_VST3 TRUE
  PLUGIN_FORMATS CLAP VST3
  ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/assets
)

foreach(module IN ITEMS simple-spectrum_clap simple-spectrum_vst3)
  if(MSVC)
    target_link_options(${module} PRIVATE /guard:cf /DYNAMICBASE /HIGHENTROPYVA)
  else()
    # The statically linked C++ runtime keeps the plugins independent of the libstdc++ version of the
    # distribution, and stays hidden. --exclude-libs,ALL would also hide the VST 3 module entry points
    # ModuleEntry and ModuleExit, which the VST 3 SDK defines in a static library.
    target_link_options(${module} PRIVATE -static-libstdc++ -static-libgcc
                        -Wl,--exclude-libs,libstdc++.a:libgcc.a:libgcc_eh.a -Wl,-z,relro,-z,now -Wl,--no-undefined)
  endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
  add_executable(analyzer-test tests/analyzer-test.cpp)
  target_link_libraries(analyzer-test PRIVATE spectrum-core)
  add_test(NAME analyzer COMMAND analyzer-test)

  add_executable(render-test tests/render-test.cpp)
  target_include_directories(render-test PRIVATE "${visage_SOURCE_DIR}/visage_graphics/third_party/stb")
  target_link_libraries(render-test PRIVATE spectrum-core)
  set(RENDER_OUTPUT "${CMAKE_BINARY_DIR}/render-output")
  file(MAKE_DIRECTORY "${RENDER_OUTPUT}")
  add_test(NAME render COMMAND render-test "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/spectrum.png" "${RENDER_OUTPUT}")

  if(SSP_SANITIZE AND NOT MSVC)
    set(sanitize_flags -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    foreach(target IN ITEMS spectrum-core pffft analyzer-test)
      target_compile_options(${target} PRIVATE ${sanitize_flags})
    endforeach()
    target_link_options(analyzer-test PRIVATE -fsanitize=address,undefined)
  endif()
endif()

# Release layout: `cmake --install build --config Release --prefix <dir>`, then zip the contents of <dir>.
# The archive carries the license texts of everything linked into the plugins. The paths below come from the
# libraries' own source trees; a layout change in a new version fails here.
set(VST3_SDK_LICENSE "${CMAKE_BINARY_DIR}/cpm/vst3sdk/LICENSE.txt")
set(CLAP_SDK_LICENSE "${clap-wrapper_BINARY_DIR}/cpm/clap/LICENSE")
set(LICENSE_FILES
  "${VST3_SDK_LICENSE}|vst3sdk.txt"
  "${CLAP_SDK_LICENSE}|clap.txt"
  "${clap-wrapper_SOURCE_DIR}/LICENSE|clap-wrapper.txt"
  "${visage_SOURCE_DIR}/LICENSE|visage.txt"
  "${visage_SOURCE_DIR}/visage_graphics/fonts/LICENSE|droid-sans-mono-apache-2.0.txt"
  "${CMAKE_CURRENT_SOURCE_DIR}/licenses/lato-OFL.txt|lato-OFL.txt"
  "${visage_SOURCE_DIR}/visage_graphics/third_party/stb/LICENSE|stb.txt"
  "${visage_SOURCE_DIR}/visage_windowing/cursors/LICENSE|lucide-cursors.txt"
  "${freetype_SOURCE_DIR}/docs/FTL.TXT|freetype-FTL.txt"
  "${pffft_SOURCE_DIR}/LICENSE.txt|pffft.txt"
)
if(UNIX AND NOT APPLE)
  # Visage embeds the Twemoji font only on Linux, and loads the Vulkan headers only there.
  list(APPEND LICENSE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/twemoji-mozilla.md|twemoji-mozilla.md"
    "${vulkan_headers_SOURCE_DIR}/LICENSE.md|vulkan-headers.md"
  )
endif()
foreach(entry IN LISTS LICENSE_FILES)
  string(REPLACE "|" ";" parts "${entry}")
  list(GET parts 0 source)
  list(GET parts 1 name)
  if(NOT EXISTS "${source}")
    message(FATAL_ERROR "License file not found: ${source}")
  endif()
  install(FILES "${source}" DESTINATION THIRD_PARTY_LICENSES RENAME "${name}")
endforeach()
# fmt keeps its license in the leading comment of format.h.
file(READ "${clap-wrapper_SOURCE_DIR}/libs/fmt/fmt/format.h" FMT_HEADER)
string(FIND "${FMT_HEADER}" "*/" FMT_COMMENT_END)
string(SUBSTRING "${FMT_HEADER}" 0 ${FMT_COMMENT_END} FMT_LICENSE_TEXT)
string(REGEX REPLACE "^/\\*\n" "" FMT_LICENSE_TEXT "${FMT_LICENSE_TEXT}")
file(WRITE "${CMAKE_BINARY_DIR}/fmt-LICENSE.txt" "${FMT_LICENSE_TEXT}")
install(FILES "${CMAKE_BINARY_DIR}/fmt-LICENSE.txt" DESTINATION THIRD_PARTY_LICENSES RENAME fmt.txt)

# clap-wrapper puts the Windows plugins in per-format folders and the Linux plugins directly in the asset folder.
if(WIN32)
  set(VST3_BUNDLE "${CMAKE_BINARY_DIR}/assets/VST3/${PRODUCT_NAME}.vst3")
else()
  set(VST3_BUNDLE "${CMAKE_BINARY_DIR}/assets/${PRODUCT_NAME}.vst3")
endif()
install(DIRECTORY "${VST3_BUNDLE}" DESTINATION VST3)
install(FILES "$<TARGET_FILE:simple-spectrum_clap>" DESTINATION CLAP)
install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
install(FILES packaging/README.txt DESTINATION .)

# Pictures for the write-up. Not part of the release.
add_executable(spectrum-demo tools/spectrum-demo.cpp)
target_link_libraries(spectrum-demo PRIVATE spectrum-core)
