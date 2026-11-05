cmake_minimum_required(VERSION 3.22)
project(LUFSCompensation VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)
if(BUILD_TESTING)
  add_library(ebur128_reference STATIC third_party/libebur128/ebur128.c)
  target_include_directories(ebur128_reference PUBLIC third_party/libebur128 PRIVATE third_party/libebur128/queue)
  if(UNIX)
    target_link_libraries(ebur128_reference PUBLIC m)
  endif()
  add_executable(loudness_tests tests/LoudnessTests.cpp)
  target_include_directories(loudness_tests PRIVATE Source)
  target_link_libraries(loudness_tests PRIVATE ebur128_reference)
  add_test(NAME loudness_reference COMMAND loudness_tests)
endif()
option(LUFS_DSP_ONLY "Build loudness tests without JUCE" OFF)
if(LUFS_DSP_ONLY)
  return()
endif()
include(FetchContent)
set(JUCE_PATH "" CACHE PATH "Optional existing JUCE 8.0.12 checkout")
if(JUCE_PATH)
  add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
else()
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850 GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(JUCE)
endif()
juce_add_plugin(LUFSCompensation
  COMPANY_NAME "Doornenbal Audio" BUNDLE_ID "nl.doornenbal.lufscompensation"
  PLUGIN_MANUFACTURER_CODE Jdau PLUGIN_CODE Lfc1
  FORMATS VST3 PRODUCT_NAME "LUFS Compensation"
  IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Fx Tools)
juce_generate_juce_header(LUFSCompensation)
target_sources(LUFSCompensation PRIVATE Source/Plugin.cpp)
target_compile_definitions(LUFSCompensation PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(LUFSCompensation PRIVATE juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(BUILD_TESTING)
  add_executable(plugin_checks tests/PluginChecks.cpp)
  target_include_directories(plugin_checks PRIVATE "${JUCE_MODULES_DIR}")
  target_compile_definitions(plugin_checks PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
  target_link_libraries(plugin_checks PRIVATE LUFSCompensation)
  add_test(NAME plugin_processing_state_ui COMMAND plugin_checks)
endif()
