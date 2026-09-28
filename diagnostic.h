#pragma once

// Boot health flags: what the performer sees as "working or not" after power-on.
// Musical role: quick triage when the box stays silent; technical role: sticky flags
// set once during setup. Volatile keeps Core 1's audio-init write visible to the
// Core 0 diagnostics that read it.
volatile bool g_audioOK = false;

// Error codes
#define ERR_NONE 0
#define ERR_AUDIO 3

// Sticky error latch: first failure wins so later stages cannot hide it.
volatile uint8_t g_errorState = ERR_NONE;
