#include <catch2/catch_test_macros.hpp>
#include "src/utils/FreezeWatchdog.h"

uint32_t g_processedStepCount = 0;

namespace
{
void resetHardware()
{
    fakeWatchdog = {};
    fakeWatchdogReset = false;
    fakeUploadReset = false;
    Serial = {};
    previousFreeze = {};
    fakeMillis = 0;
    millisCalls = 0;
    watchdogFeeds = 0;
    g_processedStepCount = 0;
}
}

TEST_CASE("Watchdog evidence survives setup and late serial reconnect", "[watchdog]")
{
    resetHardware();
    fakeWatchdogReset = true;
    fakeWatchdog.scratch[0] = FW_LOOP_TILES;
    fakeWatchdog.scratch[1] = 12345;
    fakeWatchdog.scratch[2] = 7;
    fakeWatchdog.scratch[3] = 42;
    freezeWatchdogBootCheck();
    REQUIRE(millisCalls == 0); // boot never waits for a host
    REQUIRE(Serial.output.empty());

    freezeWatchdogArm();
    fakeMillis = 500;
    freezeWatchdogFeed(FW_SETUP_UCLOCK);
    freezeWatchdogPrintPreviousRun();
    REQUIRE(Serial.output.empty());

    Serial.connected = true;
    freezeWatchdogPrintPreviousRun();
    REQUIRE(Serial.output.find("boot=7 phase=loop: Alchemy tile scan") != std::string::npos);
    REQUIRE(Serial.output.find("ms=12345 steps=42") != std::string::npos);
    const auto firstReport = Serial.output;
    Serial.connected = false;
    Serial.output.clear();
    freezeWatchdogPrintPreviousRun();
    Serial.connected = true;
    freezeWatchdogPrintPreviousRun();
    REQUIRE(Serial.output == firstReport);
}

TEST_CASE("Normal boot does not report stale watchdog scratch", "[watchdog]")
{
    resetHardware();
    fakeWatchdog.scratch[0] = FW_LOOP_LEDS;
    fakeWatchdog.scratch[2] = 5;
    Serial.connected = true;
    freezeWatchdogBootCheck();
    freezeWatchdogPrintPreviousRun();
    REQUIRE(Serial.output.empty());
}

TEST_CASE("Detailed breadcrumbs retain the slice watchdog deadline", "[watchdog]")
{
    resetHardware();
    freezeWatchdogFeed(FW_LOOP_CONTROL);
    fakeMillis = 876;
    g_processedStepCount = 12;
    freezeWatchdogMark(FW_LOOP_DISTANCE);
    REQUIRE(watchdogFeeds == 1);
    REQUIRE(fakeWatchdog.scratch[0] == FW_LOOP_DISTANCE);
    REQUIRE(fakeWatchdog.scratch[1] == 876);
    REQUIRE(fakeWatchdog.scratch[3] == 12);
    REQUIRE(FW_LOOP_CONTROL == 14); // persisted pre-refactor phase IDs
    REQUIRE(FW_LOOP_DISPLAY == 15);
}

TEST_CASE("USB upload reboot does not enter watchdog recovery", "[watchdog]")
{
    resetHardware();
    fakeUploadReset = true;
    fakeWatchdog.scratch[0] = FW_LOOP_CONTROL;
    REQUIRE(watchdog_caused_reboot());
    freezeWatchdogBootCheck();
    REQUIRE_FALSE(previousFreeze.watchdogReset);
}
