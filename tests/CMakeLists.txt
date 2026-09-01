cmake_minimum_required(VERSION 3.16)

include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

set(STUB_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
set(SRC_DIR   ${PROJECT_SOURCE_DIR}/src)
set(CORE_DIR  ${SRC_DIR}/pico2seq-core)
set(RPDSP_DIR ${SRC_DIR}/rpdsp/src)

add_executable(pico2seq_tests
  unit/test_helpers.cpp
  unit/test_rpdsp_additions.cpp
  unit/test_scales.cpp
  unit/test_sequencer.cpp
  unit/test_voice.cpp
  unit/test_voiceoscillator.cpp
  unit/test_control_surface_logic.cpp

  # Scale data
  ${CORE_DIR}/scales/scales.cpp

  # Sequencer logic
  ${CORE_DIR}/sequencer/ParameterManager.cpp
  ${CORE_DIR}/sequencer/Sequencer.cpp

  # Voice synthesis (DSP comes from header-only src/rpdsp)
  ${SRC_DIR}/voice/Voice.cpp
  ${SRC_DIR}/voice/VoicePresets.cpp

  # Alchemy tile control surface decision logic (pure C++)
  ${SRC_DIR}/ui/ControlSurfaceLogic.cpp
)

# Stub headers must come first so they shadow Arduino.h / Wire.h / pico/sync.h
target_include_directories(pico2seq_tests PRIVATE
  ${STUB_DIR}
  ${PROJECT_SOURCE_DIR}
  ${SRC_DIR}
  ${CORE_DIR}
  ${RPDSP_DIR}
)

target_link_libraries(pico2seq_tests PRIVATE Catch2::Catch2WithMain)

target_compile_options(pico2seq_tests PRIVATE
  -Wall
  -Wno-unused-parameter
)

include(CTest)
include(Catch)
catch_discover_tests(pico2seq_tests)
