cmake_minimum_required(VERSION 3.22)
# à définir AVANT project() sinon le binaire n'est pas universel
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(DOT50 VERSION 0.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)

# libwebp : décodage des images de l'arme (compactes, décodées image par image à la volée)
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_ENABLE_SIMD OFF CACHE BOOL "" FORCE)      # binaire universel arm64+x86_64 : pas d'options SIMD x86 pour les deux
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libwebp GIT_REPOSITORY https://github.com/webmproject/libwebp.git GIT_TAG v1.4.0 GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(JUCE libwebp)

if(NOT DEFINED DOT50_COPY_AFTER_BUILD)
  set(DOT50_COPY_AFTER_BUILD TRUE)
endif()

if(APPLE)
  set(DOT50_FORMATS VST3 AU Standalone)
else()
  set(DOT50_FORMATS VST3 Standalone)
endif()

juce_add_binary_data(Dot50Assets SOURCES
  plugin/Assets/cs_regular.ttf
  plugin/Assets/fire.pack
  plugin/Assets/idle.pack
  plugin/Assets/inspect.pack
  plugin/Assets/reload.pack
  plugin/Assets/shot.wav
  plugin/Assets/reload.wav)

juce_add_plugin(Dot50
  COMPANY_NAME "BUYORBEALOSER"
  PRODUCT_NAME "DOT50"
  PLUGIN_MANUFACTURER_CODE Bobl
  PLUGIN_CODE D50c
  FORMATS ${DOT50_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD ${DOT50_COPY_AFTER_BUILD}
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_CATEGORIES Fx)

target_sources(Dot50 PRIVATE plugin/Source/PluginProcessor.cpp plugin/Source/PluginEditor.cpp)
target_include_directories(Dot50 PRIVATE dsp plugin/Source ${libwebp_SOURCE_DIR}/src)
target_compile_definitions(Dot50 PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Dot50
  PRIVATE Dot50Assets webp juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Mesures DSP (sans JUCE) : cmake --build . --target frange_cli
add_executable(frange_cli tests/cli.cpp)
target_include_directories(frange_cli PRIVATE dsp)

# Outil de contrôle CI (rend l'interface en PNG sans écran + test audio) : -DDOT50_SNAPSHOT_TOOL=ON
option(DOT50_SNAPSHOT_TOOL "Build the headless snapshot/smoke-test tool" OFF)
if(DOT50_SNAPSHOT_TOOL)
  juce_add_console_app(Dot50Snap PRODUCT_NAME "dot50_snap")
  target_sources(Dot50Snap PRIVATE tools/Snap.cpp plugin/Source/PluginProcessor.cpp plugin/Source/PluginEditor.cpp)
  target_include_directories(Dot50Snap PRIVATE dsp plugin/Source ${libwebp_SOURCE_DIR}/src)
  target_compile_definitions(Dot50Snap PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(Dot50Snap PRIVATE Dot50Assets webp juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
