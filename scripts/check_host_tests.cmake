# check_host_tests.cmake - the one command that says whether the host test suite is "green".
#
#   cmake -P scripts/check_host_tests.cmake                         (any OS, from anywhere)
#   cmake -DBUILD_DIR=build_test_ninja -P scripts/check_host_tests.cmake
#   cmake -DBASELINE=tests/known_failures.windows.txt -P scripts/check_host_tests.cmake
#
# Why this exists: about thirty host tests already fail on a clean checkout
# (tests/known_failures.txt), so "every test passes" can never be the gate, and a bare failure
# count hides a new failure behind a fixed one. This script compares the failing test NAMES
# with that list and fails on:
#   - a failing test that is not listed (a regression);
#   - a listed test that now passes (delete its line, or a later regression of it would stay
#     hidden behind the list);
#   - a configure or build failure (a stale binary must never be mistaken for a pass);
#   - a CTest run with no summary (zero tests discovered is not green).
# Firmware-only code (src/OLED, src/LEDMatrix, Matrix.cpp, the Wire-bound UI files) is not in the
# host suite; compiling it is a separate check, see .delta/skills/land/SKILL.md.
cmake_minimum_required(VERSION 3.16)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT BUILD_DIR)
  set(BUILD_DIR "build_test_ninja")
endif()
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE BASE_DIR "${REPO_ROOT}")
# The recorded list is Linux/GCC. A toolchain whose failing set differs (Windows/clang, fast-math)
# keeps its own file and passes it with -DBASELINE=...; the comparison logic is the same.
if(NOT BASELINE)
  set(BASELINE "tests/known_failures.txt")
endif()
get_filename_component(BASELINE_FILE "${BASELINE}" ABSOLUTE BASE_DIR "${REPO_ROOT}")
if(NOT EXISTS "${BASELINE_FILE}")
  message(FATAL_ERROR "check_host_tests: baseline file not found: ${BASELINE_FILE}")
endif()
string(ASCII 9 TAB)

# --- configure + build (streamed; any failure stops here) ------------------------------------
set(generator_args "")
find_program(NINJA_EXE ninja)
if(NINJA_EXE AND NOT EXISTS "${BUILD_DIR}/CMakeCache.txt")
  set(generator_args -G Ninja)
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${REPO_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug ${generator_args}
  RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "check_host_tests: configure failed. On a fresh clone run "
                      "'git submodule update --init --recursive' first.")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --config Debug --parallel
  RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "check_host_tests: build failed; the test binaries are stale, so no result is trustworthy.")
endif()

# --- run the suite and collect the failing names ---------------------------------------------
get_filename_component(CMAKE_BIN_DIR "${CMAKE_COMMAND}" DIRECTORY)
find_program(CTEST_EXE ctest HINTS "${CMAKE_BIN_DIR}" NO_DEFAULT_PATH)
if(NOT CTEST_EXE)
  find_program(CTEST_EXE ctest)
endif()
if(NOT CTEST_EXE)
  message(FATAL_ERROR "check_host_tests: ctest not found next to cmake.")
endif()
# Failures make ctest exit non-zero by design, so its exit code is not the verdict.
execute_process(
  COMMAND "${CTEST_EXE}" -C Debug
  WORKING_DIRECTORY "${BUILD_DIR}"
  OUTPUT_VARIABLE ctest_out
  ERROR_VARIABLE ctest_err)
string(REPLACE "\r\n" "\n" ctest_out "${ctest_out}")

string(REGEX MATCH "([0-9]+)% tests passed, ([0-9]+) tests failed out of ([0-9]+)" summary "${ctest_out}")
if(NOT summary OR CMAKE_MATCH_3 EQUAL 0)
  message(FATAL_ERROR "check_host_tests: ctest printed no usable summary (no tests discovered?):\n${ctest_out}${ctest_err}")
endif()
set(total_tests "${CMAKE_MATCH_3}")

# Lines of the form "<TAB>985 - voice-focused:Some test name (Failed)" in ctest's final list.
set(failed_names "")
string(REGEX MATCHALL "\n[ ${TAB}]+[0-9]+ - [^\n]+ \\([A-Za-z ]+\\)" failed_lines "${ctest_out}")
foreach(line IN LISTS failed_lines)
  string(REGEX REPLACE "^\n[ ${TAB}]+[0-9]+ - (.*) \\([A-Za-z ]+\\)$" "\\1" name "${line}")
  # CMake lists are ';'-separated, so a ';' in a test name would silently split it.
  if(name MATCHES ";")
    message(FATAL_ERROR "check_host_tests: test name contains ';' (unsupported): ${name}")
  endif()
  list(APPEND failed_names "${name}")
endforeach()

# --- compare with the recorded baseline ------------------------------------------------------
set(known_names "")
file(STRINGS "${BASELINE_FILE}" baseline_lines)
foreach(line IN LISTS baseline_lines)
  string(STRIP "${line}" line)
  if(NOT line STREQUAL "" AND NOT line MATCHES "^#")
    list(APPEND known_names "${line}")
  endif()
endforeach()

set(new_failures ${failed_names})
if(known_names)
  list(REMOVE_ITEM new_failures ${known_names})
endif()
set(now_passing ${known_names})
if(failed_names)
  list(REMOVE_ITEM now_passing ${failed_names})
endif()

list(LENGTH failed_names failed_count)
list(LENGTH known_names known_count)
message(STATUS "check_host_tests: ${total_tests} tests, ${failed_count} failing, ${known_count} in the baseline list")

set(problems "")
foreach(name IN LISTS new_failures)
  string(APPEND problems "\n  NEW FAILURE (not in the baseline list): ${name}")
endforeach()
foreach(name IN LISTS now_passing)
  string(APPEND problems "\n  NOW PASSING (delete its line from the baseline list): ${name}")
endforeach()
if(problems)
  message(FATAL_ERROR "check_host_tests: FAILED${problems}")
endif()
message(STATUS "check_host_tests: OK - no new failures, baseline list is current")
