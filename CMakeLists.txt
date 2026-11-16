cmake_minimum_required(VERSION 3.21)

project(simple-spectrum-audio VERSION 0.0.0 LANGUAGES C CXX)

# Visage requires C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

include(FetchContent)
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_BUILD_AUV2 FALSE CACHE BOOL "" FORCE)
FetchContent_Declare(
  clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG 806bba0a5f792e3966b99c1561ab9952d3316d7c
)
FetchContent_MakeAvailable(clap-wrapper)

# Visage v1.0.0, pinned by commit and SHA-256.
set(VISAGE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VISAGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# X11 only: a plug-in window cannot be embedded in a host window on Wayland, and the plug-in would need
# libwayland-client and libxkbcommon at run time.
set(VISAGE_WAYLAND OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  visage
  URL https://github.com/VitalAudio/visage/archive/5754ff8184cd0ba47a6f0a54a5f4a71974301036.tar.gz
  URL_HASH SHA256=aaa0268e7d50cd460c2a4c851337912215c0aa0e17ac8ba687ff0ba378829e7d
)
FetchContent_MakeAvailable(visage)

set(PRODUCT_NAME "Simple Spectrum")

add_library(spectrum-view STATIC src/spectrum-view.cpp)
target_include_directories(spectrum-view PUBLIC src)
target_link_libraries(spectrum-view PUBLIC visage)

add_library(simple-spectrum-impl STATIC src/plugin.cpp)
target_include_directories(simple-spectrum-impl PRIVATE "${clap-wrapper_SOURCE_DIR}/include")
target_link_libraries(simple-spectrum-impl PUBLIC clap spectrum-view)

make_clapfirst_plugins(
  TARGET_NAME simple-spectrum
  IMPL_TARGET simple-spectrum-impl
  OUTPUT_NAME "${PRODUCT_NAME}"
  ENTRY_SOURCE src/plugin-entry.cpp
  BUNDLE_IDENTIFIER "jp.allegromoltov.simple-spectrum"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  WINDOWS_FOLDER_VST3 TRUE
  PLUGIN_FORMATS CLAP VST3
  ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/assets
)

if(NOT MSVC)
  foreach(module IN ITEMS simple-spectrum_clap simple-spectrum_vst3)
    target_link_options(${module} PRIVATE -static-libstdc++ -static-libgcc
                        -Wl,--exclude-libs,libstdc++.a:libgcc.a:libgcc_eh.a -Wl,-z,relro,-z,now -Wl,--no-undefined)
  endforeach()
endif()

add_executable(render-test tests/render-test.cpp)
target_link_libraries(render-test PRIVATE spectrum-view)
