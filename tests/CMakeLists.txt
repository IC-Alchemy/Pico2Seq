cmake_minimum_required(VERSION 3.16)

include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

set(STUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
set(SRC_DIR  ${PROJECT_SOURCE_DIR}/src)
set(LIB_DIR  ${PROJECT_SOURCE_DIR}/lib/pico2seq-core)

add_executable(pico2seq_tests
  unit/test_helpers.cpp
  unit/test_dsp.cpp
  unit/test_scales.cpp
  unit/test_sequencer.cpp
  unit/test_voice.cpp

  # DSP sources (no hardware deps)
  ${SRC_DIR}/dsp/adsr.cpp
  ${SRC_DIR}/dsp/oscillator.cpp
  ${SRC_DIR}/dsp/ladder.cpp
  ${SRC_DIR}/dsp/svf.cpp
  ${SRC_DIR}/dsp/overdrive.cpp
  ${SRC_DIR}/dsp/wavefolder.cpp
  ${SRC_DIR}/dsp/dcblock.cpp
  ${SRC_DIR}/dsp/tremolo.cpp

  # Scale data
  ${LIB_DIR}/scales/scales.cpp

  # Sequencer logic
  ${LIB_DIR}/sequencer/ParameterManager.cpp
  ${LIB_DIR}/sequencer/Sequencer.cpp

  # Voice synthesis
  ${SRC_DIR}/voice/Voice.cpp
  ${SRC_DIR}/voice/VoicePresets.cpp
)

# Stub headers must come first so they shadow Arduino.h / Wire.h / pico/sync.h
target_include_directories(pico2seq_tests PRIVATE
  ${STUB_DIR}
  ${PROJECT_SOURCE_DIR}
  ${SRC_DIR}
  ${LIB_DIR}
)

target_link_libraries(pico2seq_tests PRIVATE Catch2::Catch2WithMain)

target_compile_options(pico2seq_tests PRIVATE
  -Wall
  -Wno-unused-parameter
)

include(CTest)
include(Catch)
catch_discover_tests(pico2seq_tests)
