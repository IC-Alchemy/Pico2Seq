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

# Fail loudly instead of drowning a fresh clone in missing-header errors:
# src/rpdsp and src/VelocityEncoder are git submodules and are empty until
# they are checked out. Same trap on the Arduino build, which needs them too.
if(NOT EXISTS "${RPDSP_DIR}/rpdsp/parameter_track.h")
  message(FATAL_ERROR
    "src/rpdsp submodule is not checked out. Run:\n"
    "  git submodule update --init --recursive")
endif()
# Checked out but too old: dark_reverb.h is the newest rpdsp header the firmware
# includes (voice/MasterReverb.h), so a pin that predates the reverb lacks it.
if(NOT EXISTS "${RPDSP_DIR}/rpdsp/dark_reverb.h")
  message(FATAL_ERROR
    "src/rpdsp is checked out at a commit without dark_reverb.h, so it is older than "
    "this tree needs. Run:\n"
    "  git submodule update --init --recursive\n"
    "and if that does not fix it, the submodule pin recorded in this repository is "
    "wrong (it must be an rpdsp commit that contains src/rpdsp/dark_reverb.h).")
endif()
if(NOT EXISTS "${SRC_DIR}/VelocityEncoder/src/MagEncoder.h")
  message(FATAL_ERROR
    "src/VelocityEncoder submodule is not checked out. Run:\n"
    "  git submodule update --init --recursive")
endif()

# Reverb tank storage the host tests build against. HALF mirrors the firmware
# default (MasterReverb.h); FLOAT re-runs the same suites against the 24-bit tank:
#   cmake -B build_test_float -DPICO2SEQ_REVERB_STORAGE=FLOAT
# Applied to every target here, so MasterReverb (embedded in VoiceManager) has one
# definition per executable.
set(PICO2SEQ_REVERB_STORAGE "HALF" CACHE STRING "DarkReverb storage for the host tests: HALF or FLOAT")
set_property(CACHE PICO2SEQ_REVERB_STORAGE PROPERTY STRINGS HALF FLOAT)
if(PICO2SEQ_REVERB_STORAGE STREQUAL "FLOAT")
  add_compile_definitions(PICO2SEQ_REVERB_STORAGE_HALF=0)
elseif(PICO2SEQ_REVERB_STORAGE STREQUAL "HALF")
  add_compile_definitions(PICO2SEQ_REVERB_STORAGE_HALF=1)
else()
  message(FATAL_ERROR "PICO2SEQ_REVERB_STORAGE must be HALF or FLOAT, not '${PICO2SEQ_REVERB_STORAGE}'")
endif()

# Portable sources compiled once, grouped the way the suites consume them.
# OBJECT rather than static libraries: each target keeps its own stub include
# dirs and link libraries, and object libraries compose without forcing a
# shared link configuration. The groups mirror the existing per-suite object
# sets (e.g. VoiceManager references test-side stubs like currentScale from
# unit/test_helpers.cpp, so the UI suite only takes the UI group).
set(PICO2SEQ_TEST_INCLUDE_DIRS
  ${STUB_DIR}
  ${PROJECT_SOURCE_DIR}
  ${SRC_DIR}
  ${CORE_DIR}
  ${RPDSP_DIR}
)

# Alchemy tile control surface decision logic + arpeggiator note engine.
add_library(pico2seq_ui_code OBJECT
  ${SRC_DIR}/ui/ControlSurfaceLogic.cpp
  ${CORE_DIR}/arpeggiator/Arpeggiator.cpp
)

# Sequencer logic and voice synthesis (DSP comes from header-only src/rpdsp). The scale tables
# live with the tuning code below: which scales a tuning offers is part of the tuning library.
add_library(pico2seq_voice_code OBJECT
  ${CORE_DIR}/sequencer/ParameterManager.cpp
  ${CORE_DIR}/sequencer/Sequencer.cpp
  ${SRC_DIR}/voice/Voice.cpp
  ${SRC_DIR}/voice/VoicePresets.cpp
  ${SRC_DIR}/voice/VoiceParameters.cpp
  ${SRC_DIR}/voice/VoiceEditParameters.cpp
  ${SRC_DIR}/voice/VoiceManager.cpp
  ${SRC_DIR}/voice/MasterReverb.cpp
)

# Session persistence (portable snapshot + codecs); only the full suite uses it.
add_library(pico2seq_persist_code OBJECT
  ${CORE_DIR}/persistence/SnapshotFormat.cpp
  ${CORE_DIR}/persistence/ProjectSnapshot.cpp
  ${CORE_DIR}/persistence/PatternCodec.cpp
  ${CORE_DIR}/persistence/UserPresetBank.cpp
  ${SRC_DIR}/voice/PatchCodec.cpp
  ${SRC_DIR}/voice/PatchFields.cpp
  ${SRC_DIR}/voice/UserPresetCodec.cpp
  ${SRC_DIR}/voice/EffectsCodec.cpp
  ${SRC_DIR}/presetlink/PresetLinkProtocol.cpp
  ${SRC_DIR}/presetlink/PresetLinkSession.cpp
  ${SRC_DIR}/presetlink/UserPresetStore.cpp
)

# Tuning library, pitch maths and the scale tables (portable, flash tables only): the voices,
# the song file and the Tuning page logic all use them, so every suite that links one of
# them links this.
add_library(pico2seq_tuning_code OBJECT
  ${CORE_DIR}/scales/scales.cpp
  ${CORE_DIR}/tuning/Tuning.cpp
  ${CORE_DIR}/tuning/TuningLibrary.cpp
  ${CORE_DIR}/tuning/TuningScales.cpp
)

foreach(_lib pico2seq_ui_code pico2seq_voice_code pico2seq_persist_code pico2seq_tuning_code)
  target_include_directories(${_lib} PRIVATE ${PICO2SEQ_TEST_INCLUDE_DIRS})
  target_compile_definitions(${_lib} PRIVATE AUG_DEBUG_COMPILED=0)
  target_compile_options(${_lib} PRIVATE -Wall -Wno-unused-parameter)
endforeach()

# Test files shared by more than one suite, listed once.
set(PICO2SEQ_UI_TEST_SOURCES
  unit/test_control_surface_logic.cpp
  unit/test_arpeggiator.cpp
  unit/test_ui_transitions.cpp
  unit/test_reverb_page.cpp
  unit/test_tuning.cpp
  unit/test_tuning_scales.cpp
  unit/test_tuning_page.cpp
)
set(PICO2SEQ_VOICE_TEST_SOURCES
  unit/test_helpers.cpp
  unit/test_voice.cpp
  unit/test_voice_recipes.cpp
  unit/test_recipe_optimization.cpp
  unit/test_voiceoscillator.cpp
  unit/test_voice_transfer.cpp
  unit/test_voice_block.cpp
  unit/test_master_compressor.cpp
  unit/test_master_bus.cpp
  unit/test_master_delay.cpp
  unit/test_master_reverb.cpp
)

add_executable(pico2seq_tests
  unit/test_rpdsp_additions.cpp
  unit/test_parameter_mapping.cpp
  unit/test_parameter_randomize.cpp
  unit/test_dsp_recipe_regressions.cpp
  unit/test_scales.cpp
  unit/test_sequencer.cpp
  unit/test_sequencer_view.cpp
  unit/test_voice_playback.cpp
  unit/test_voice_envelope.cpp
  unit/test_reverb_editor.cpp
  unit/test_stack_watermark.cpp
  unit/test_serial_rate_limit.cpp
  unit/test_lidar_recording.cpp
  ${SRC_DIR}/app/VoicePlayback.cpp
  ${SRC_DIR}/app/VoiceEnvelope.cpp
  ${SRC_DIR}/app/ReverbEditor.cpp
  ${SRC_DIR}/app/StepPlayback.cpp
  unit/test_alchemy_proto.cpp
  unit/test_satellite_link.cpp
  unit/test_app_runtime.cpp
  unit/test_voice_edit.cpp
  unit/test_settings_pads.cpp
  unit/test_persistence.cpp
  unit/test_user_presets.cpp
  unit/test_preset_link.cpp
  unit/test_preset_browser.cpp
  unit/test_preset_resources.cpp
  ${PICO2SEQ_UI_TEST_SOURCES}
  ${PICO2SEQ_VOICE_TEST_SOURCES}
  $<TARGET_OBJECTS:pico2seq_ui_code>
  $<TARGET_OBJECTS:pico2seq_voice_code>
  $<TARGET_OBJECTS:pico2seq_persist_code>
  $<TARGET_OBJECTS:pico2seq_tuning_code>
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
target_compile_definitions(pico2seq_tests PRIVATE AUG_DEBUG_COMPILED=0
  PICO2SEQ_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

target_compile_options(pico2seq_tests PRIVATE
  -Wall
  -Wno-unused-parameter
)

include(CTest)
include(Catch)
catch_discover_tests(pico2seq_tests)

# UI policies can be checked without linking firmware glue or rendering audio.
add_executable(pico2seq_ui_tests
  ${PICO2SEQ_UI_TEST_SOURCES}
  $<TARGET_OBJECTS:pico2seq_ui_code>
  $<TARGET_OBJECTS:pico2seq_tuning_code>)
target_include_directories(pico2seq_ui_tests PRIVATE
  ${STUB_DIR} ${PROJECT_SOURCE_DIR} ${SRC_DIR} ${CORE_DIR} ${RPDSP_DIR})
target_link_libraries(pico2seq_ui_tests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(pico2seq_ui_tests TEST_PREFIX "ui-focused: ")

# Focused voice/ownership suite can run independently of the DSP recipe tests.
add_executable(pico2seq_voice_tests
  ${PICO2SEQ_VOICE_TEST_SOURCES}
  $<TARGET_OBJECTS:pico2seq_voice_code>
  $<TARGET_OBJECTS:pico2seq_tuning_code>)
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

# The real tile driver against a scriptable I2C bus. Covers the one-transaction
# snapshot poll and the sequence/timeout/last-known-good behaviour that keeps a
# flaky satellite out of the audio path; the Wire stub here shadows the no-op
# one in tests/stubs, which still supplies Arduino.h.
add_executable(pico2seq_tile_tests
  unit/test_alchemy_tiles.cpp
  ${SRC_DIR}/AlchemyUI/src/AlchemyTiles.cpp
)
target_include_directories(pico2seq_tile_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/tile_stubs ${STUB_DIR} ${SRC_DIR})
target_link_libraries(pico2seq_tile_tests PRIVATE Catch2::Catch2WithMain)
target_compile_options(pico2seq_tile_tests PRIVATE -Wall -Wno-unused-parameter)
catch_discover_tests(pico2seq_tile_tests TEST_PREFIX "tiles: ")

# The PY32 tile sketches themselves, compiled unmodified against a host shim
# for the PY32Duino core and driven through their I2C slave ISR by a modelled
# master (tests/py32_stubs/). Covers the slave state machine and the publish
# path -- the half of the satellite contract the hub cannot check from its end.
# Each sketch gets its own binary: they define the same symbols.
add_executable(py32_slider_tests
  unit/test_py32_slider_tile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/py32_stubs/py32_stubs.cpp
)
target_include_directories(py32_slider_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/py32_stubs
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/tiles)
target_link_libraries(py32_slider_tests PRIVATE Catch2::Catch2WithMain)
target_compile_options(py32_slider_tests PRIVATE -Wall -Wno-unused-parameter
  -Wno-unused-function -Wno-unused-variable -x c++)
catch_discover_tests(py32_slider_tests TEST_PREFIX "py32-slider: ")

add_executable(py32_button_tests
  unit/test_py32_button_tile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/py32_stubs/py32_stubs.cpp
)
target_include_directories(py32_button_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/py32_stubs
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/tiles)
target_link_libraries(py32_button_tests PRIVATE Catch2::Catch2WithMain)
target_compile_options(py32_button_tests PRIVATE -Wall -Wno-unused-parameter
  -Wno-unused-function -Wno-unused-variable -x c++)
catch_discover_tests(py32_button_tests TEST_PREFIX "py32-button: ")

# The firmware's preset link as a host process, for the PC editor's end-to-end tests
# (tools/PresetStudio): same parser, session, store and validator as the firmware, with a
# file for flash and plain voices. Built with the suites so the C# tests can find it.
add_executable(preset_link_sim
  tools/preset_link_sim.cpp
  unit/test_helpers.cpp
  $<TARGET_OBJECTS:pico2seq_persist_code>
  $<TARGET_OBJECTS:pico2seq_voice_code>
  $<TARGET_OBJECTS:pico2seq_tuning_code>)
target_include_directories(preset_link_sim PRIVATE
  ${STUB_DIR} ${PROJECT_SOURCE_DIR} ${SRC_DIR} ${CORE_DIR} ${RPDSP_DIR})
target_link_libraries(preset_link_sim PRIVATE Threads::Threads)
target_compile_definitions(preset_link_sim PRIVATE AUG_DEBUG_COMPILED=0)
target_compile_options(preset_link_sim PRIVATE -Wall -Wno-unused-parameter)

# Opt-in throughput measurement; timing thresholds do not belong in CTest.
add_executable(pico2seq_recipe_benchmark EXCLUDE_FROM_ALL benchmarks/recipe_benchmark.cpp)
target_include_directories(pico2seq_recipe_benchmark PRIVATE ${SRC_DIR})
