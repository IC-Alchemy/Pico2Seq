# Desktop engine for the Pico2Seq Windows port (branch x86).
#
# p2s_engine  : OBJECT library compiling the FIRMWARE'S OWN SOURCES (engine
#               + app plane) against desktop/native/shims, plus the desktop
#               adapters (HostClock, DesktopAudio, DesktopStorage, DesktopApp).
# p2s_desktop : SHARED library exposing the flat C ABI (api.h) for the WinUI
#               front end.
# p2s_desktop_tests : Catch2 golden tests (clock timing, headless soak).

cmake_minimum_required(VERSION 3.16)

include(FetchContent)

FetchContent_Declare(
  miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG        0.11.22
)
FetchContent_MakeAvailable(miniaudio)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# The engine object library is MT (static CRT) for the shipped DLL; the test
# exe links those objects, so Catch2 must use the same runtime.
set_target_properties(Catch2 Catch2WithMain PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(DESKTOP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SHIMS_DIR ${DESKTOP_DIR}/shims)
set(REPO_ROOT ${DESKTOP_DIR}/../..)
set(SRC_DIR ${REPO_ROOT}/src)
set(CORE_DIR ${SRC_DIR}/pico2seq-core)
set(RPDSP_DIR ${SRC_DIR}/rpdsp/src)

# ---------------------------------------------------------------------------
# miniaudio (WASAPI backend) — the upstream CMakeLists provides the target
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Engine object library: firmware sources + desktop adapters
# ---------------------------------------------------------------------------
add_library(p2s_engine OBJECT
  # Portable core (identical to the host test suite)
  ${CORE_DIR}/scales/scales.cpp
  ${CORE_DIR}/sequencer/ParameterManager.cpp
  ${CORE_DIR}/sequencer/Sequencer.cpp
  ${CORE_DIR}/persistence/SnapshotFormat.cpp
  ${CORE_DIR}/persistence/ProjectSnapshot.cpp
  ${CORE_DIR}/persistence/PatternCodec.cpp

  # Voice synthesis stack
  ${SRC_DIR}/voice/Voice.cpp
  ${SRC_DIR}/voice/VoicePresets.cpp
  ${SRC_DIR}/voice/VoiceParameters.cpp
  ${SRC_DIR}/voice/VoiceEditParameters.cpp
  ${SRC_DIR}/voice/VoiceManager.cpp
  ${SRC_DIR}/voice/PatchCodec.cpp

  # App plane (compiled against the desktop shims)
  ${SRC_DIR}/app/AppState.cpp
  ${SRC_DIR}/app/ControlIO.cpp
  ${SRC_DIR}/app/ClockService.cpp
  ${SRC_DIR}/app/StepPlayback.cpp
  ${SRC_DIR}/app/VoiceSetup.cpp
  ${SRC_DIR}/app/Session.cpp
  ${SRC_DIR}/app/VoiceEditor.cpp

  # Input + display plane
  ${SRC_DIR}/matrix/Matrix.cpp
  ${SRC_DIR}/OLED/oled.cpp
  ${SRC_DIR}/sensors/DistanceSensor.cpp
  ${SRC_DIR}/AlchemyUI/src/AlchemyTiles.cpp

  # UI decision logic
  ${SRC_DIR}/ui/ControlSurfaceLogic.cpp
  ${SRC_DIR}/ui/ButtonManager.cpp
  ${SRC_DIR}/ui/ButtonHandlers.cpp
  ${SRC_DIR}/ui/UIEventHandler.cpp
  ${SRC_DIR}/ui/AlchemyControlBridge.cpp

  # Internal note/gate lifecycle bookkeeping
  ${SRC_DIR}/midi/MidiManager.cpp

  # Encoder target/velocity logic (+ the VelocityEncoder driver core it owns)
  ${SRC_DIR}/sensors/EncoderManager.cpp
  ${SRC_DIR}/VelocityEncoder/src/MagEncoder.cpp
  ${SRC_DIR}/VelocityEncoder/src/TMAG5273.cpp

  # LED feedback decisions (rendered to virtual pixels)
  ${SRC_DIR}/LEDMatrix/ledMatrix.cpp
  ${SRC_DIR}/LEDMatrix/LEDMatrixFeedback.cpp

  # Desktop adapters
  ${DESKTOP_DIR}/src/shim_runtime.cpp
  ${DESKTOP_DIR}/src/HostClock.cpp
  ${DESKTOP_DIR}/src/DesktopAudio.cpp
  ${DESKTOP_DIR}/src/DesktopStorage.cpp
  ${DESKTOP_DIR}/src/DesktopApp.cpp
  ${DESKTOP_DIR}/src/DisplayBridge.cpp
  ${DESKTOP_DIR}/src/globals.cpp
)

set_target_properties(p2s_engine PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  # Static CRT for the shipped DLL: a packaged (MSIX) app cannot rely on the
  # debug redistributables; Release + static keeps KERNEL32 the only import.
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_target_properties(miniaudio PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Shim headers must come first so they shadow Arduino.h / Wire.h / uClock.h /
# FastLED.h / hardware/sync.h exactly the way tests/stubs does for the tests.
target_include_directories(p2s_engine BEFORE PRIVATE
  ${SHIMS_DIR}
  ${REPO_ROOT}
  ${SRC_DIR}
  ${CORE_DIR}
  ${RPDSP_DIR}
  ${DESKTOP_DIR}/src
)

find_package(Threads REQUIRED)
target_link_libraries(p2s_engine PUBLIC Threads::Threads miniaudio)
target_compile_definitions(p2s_engine PRIVATE P2S_DESKTOP_BUILD=1 AUG_DEBUG_COMPILED=0 _CRT_SECURE_NO_WARNINGS)

if(MSVC)
  target_compile_options(p2s_engine PRIVATE /utf-8 /Zc:__cplusplus)
  target_compile_definitions(p2s_engine PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(p2s_engine PRIVATE -Wall -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# DLL with the C ABI for the WinUI front end
# ---------------------------------------------------------------------------
add_library(p2s_desktop SHARED
  $<TARGET_OBJECTS:p2s_engine>
  ${DESKTOP_DIR}/src/api.cpp
)
target_include_directories(p2s_desktop PRIVATE
  ${DESKTOP_DIR}/src ${SRC_DIR} ${CORE_DIR} ${SHIMS_DIR})
target_compile_definitions(p2s_desktop PRIVATE P2S_DLL_EXPORTS=1)
target_link_libraries(p2s_desktop PRIVATE miniaudio Threads::Threads)
set_target_properties(p2s_desktop PROPERTIES
  OUTPUT_NAME "p2s_desktop"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Golden tests
# ---------------------------------------------------------------------------
add_executable(p2s_desktop_tests
  tests/test_host_clock.cpp
  tests/test_headless.cpp
  $<TARGET_OBJECTS:p2s_engine>
)
target_include_directories(p2s_desktop_tests BEFORE PRIVATE
  ${SHIMS_DIR} ${REPO_ROOT} ${SRC_DIR} ${CORE_DIR} ${RPDSP_DIR} ${DESKTOP_DIR}/src)
target_link_libraries(p2s_desktop_tests PRIVATE Catch2::Catch2WithMain Threads::Threads miniaudio)
if(MSVC)
  target_compile_options(p2s_desktop_tests PRIVATE /utf-8 /Zc:__cplusplus)
endif()

include(CTest)
if(BUILD_TESTING)
  add_test(NAME desktop_tests COMMAND p2s_desktop_tests)
endif()
