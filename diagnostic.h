#pragma once

// Diagnostic flags
volatile bool g_serialOK = false;
volatile bool g_midiOK = false;
volatile bool g_audioOK = false;
volatile bool g_sensorOK = false;

// Error codes
#define ERR_NONE 0
#define ERR_SERIAL 1
#define ERR_MIDI 2
#define ERR_AUDIO 3
#define ERR_SENSOR 4

// Global error state
volatile uint8_t g_errorState = ERR_NONE;
