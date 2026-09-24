#pragma once

// Boot health flags: what the performer sees as "working or not" after power-on.
// Musical role: quick triage when the box stays silent; technical role: sticky flags
// set once during setup. Core 0 writes them; Core 0 diagnostics read them.
// Volatile: kept from the pre-dual-core layout; no cross-core sync intended.
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

// Sticky error latch: first failure wins so later stages cannot hide it.
volatile uint8_t g_errorState = ERR_NONE;
