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
  unit/test_parameter_mapping.cpp
  unit/test_parameter_randomize.cpp
  unit/test_dsp_recipe_regressions.cpp
  unit/test_scales.cpp
  unit/test_sequencer.cpp
  unit/test_sequencer_view.cpp
  unit/test_voice_playback.cpp
  ${SRC_DIR}/app/VoicePlayback.cpp
  unit/test_voice.cpp
  unit/test_voice_recipes.cpp
  unit/test_recipe_optimization.cpp
  unit/test_voice_transfer.cpp
  unit/test_voice_block.cpp
  unit/test_voiceoscillator.cpp
  unit/test_control_surface_logic.cpp
  unit/test_arpeggiator.cpp
  unit/test_ui_transitions.cpp
  unit/test_alchemy_proto.cpp
  unit/test_app_runtime.cpp
  unit/test_voice_edit.cpp
  unit/test_settings_pads.cpp
  unit/test_persistence.cpp

  # Scale data
  ${CORE_DIR}/scales/scales.cpp

  # Arpeggiator mode note engine (portable chord/pattern/clock state machine)
  ${CORE_DIR}/arpeggiator/Arpeggiator.cpp

  # Sequencer logic
  ${CORE_DIR}/sequencer/ParameterManager.cpp
  ${CORE_DIR}/sequencer/Sequencer.cpp

  # Session persistence (portable snapshot + codecs)
  ${CORE_DIR}/persistence/SnapshotFormat.cpp
  ${CORE_DIR}/persistence/ProjectSnapshot.cpp
  ${CORE_DIR}/persistence/PatternCodec.cpp

  # Voice synthesis (DSP comes from header-only src/rpdsp)
  ${SRC_DIR}/voice/Voice.cpp
  ${SRC_DIR}/voice/VoicePresets.cpp
  ${SRC_DIR}/voice/VoiceParameters.cpp
  ${SRC_DIR}/voice/VoiceEditParameters.cpp
  ${SRC_DIR}/voice/VoiceManager.cpp
  ${SRC_DIR}/voice/PatchCodec.cpp

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

find_package(Threads REQUIRED)
target_link_libraries(pico2seq_tests PRIVATE Catch2::Catch2WithMain Threads::Threads)
target_compile_definitions(pico2seq_tests PRIVATE AUG_DEBUG_COMPILED=0)

target_compile_options(pico2seq_tests PRIVATE
  -Wall
  -Wno-unused-parameter
)

include(CTest)
include(Catch)
catch_discover_tests(pico2seq_tests)

# UI policies can be checked without linking firmware glue or rendering audio.
add_executable(pico2seq_ui_tests
  unit/test_control_surface_logic.cpp
  unit/test_arpeggiator.cpp
  unit/test_ui_transitions.cpp
  ${SRC_DIR}/ui/ControlSurfaceLogic.cpp
  ${CORE_DIR}/arpeggiator/Arpeggiator.cpp)
target_include_directories(pico2seq_ui_tests PRIVATE
  ${STUB_DIR} ${PROJECT_SOURCE_DIR} ${SRC_DIR} ${CORE_DIR} ${RPDSP_DIR})
target_link_libraries(pico2seq_ui_tests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(pico2seq_ui_tests TEST_PREFIX "ui-focused: ")

# Focused voice/ownership suite can run independently of the DSP recipe tests.
add_executable(pico2seq_voice_tests
  unit/test_helpers.cpp
  unit/test_voice.cpp
  unit/test_voice_recipes.cpp
  unit/test_recipe_optimization.cpp
  unit/test_voiceoscillator.cpp
  unit/test_voice_transfer.cpp
  unit/test_voice_block.cpp
  ${CORE_DIR}/scales/scales.cpp
  ${CORE_DIR}/sequencer/ParameterManager.cpp
  ${CORE_DIR}/sequencer/Sequencer.cpp
  ${SRC_DIR}/voice/Voice.cpp
  ${SRC_DIR}/voice/VoicePresets.cpp
  ${SRC_DIR}/voice/VoiceParameters.cpp
  ${SRC_DIR}/voice/VoiceEditParameters.cpp
  ${SRC_DIR}/voice/VoiceManager.cpp
)
target_include_directories(pico2seq_voice_tests PRIVATE
  ${STUB_DIR} ${PROJECT_SOURCE_DIR} ${SRC_DIR} ${CORE_DIR} ${RPDSP_DIR})
target_link_libraries(pico2seq_voice_tests PRIVATE Catch2::Catch2WithMain Threads::Threads)
target_compile_definitions(pico2seq_voice_tests PRIVATE AUG_DEBUG_COMPILED=0)
target_compile_options(pico2seq_voice_tests PRIVATE -Wall -Wno-unused-parameter)
catch_discover_tests(pico2seq_voice_tests TEST_PREFIX "voice-focused: "
  PROPERTIES TIMEOUT 60)

# Exercise the real watchdog report code with isolated hardware/serial stubs.
add_executable(pico2seq_watchdog_tests unit/test_freeze_watchdog.cpp)
target_include_directories(pico2seq_watchdog_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/watchdog_stubs ${PROJECT_SOURCE_DIR})
target_link_libraries(pico2seq_watchdog_tests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(pico2seq_watchdog_tests TEST_PREFIX "watchdog: ")

# Real pool and I2S driver with deterministic DMA/PIO doubles. Covers buffer
# ownership and underrun recovery; target timing still requires hardware.
add_executable(pico2seq_audio_tests unit/test_audio_i2s.cpp)
target_include_directories(pico2seq_audio_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/audio_stubs ${PROJECT_SOURCE_DIR})
target_compile_definitions(pico2seq_audio_tests PRIVATE PICO_RP2350=1 ARDUINO_ARCH_RP2040=1)
target_link_libraries(pico2seq_audio_tests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(pico2seq_audio_tests TEST_PREFIX "audio: ")

# Opt-in throughput measurement; timing thresholds do not belong in CTest.
add_executable(pico2seq_recipe_benchmark EXCLUDE_FROM_ALL benchmarks/recipe_benchmark.cpp)
target_include_directories(pico2seq_recipe_benchmark PRIVATE ${SRC_DIR})
