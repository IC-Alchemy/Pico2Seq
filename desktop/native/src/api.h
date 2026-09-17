#ifndef P2S_DESKTOP_API_H
#define P2S_DESKTOP_API_H

// Flat C ABI between p2s_desktop.dll and the WinUI (C#) front end.
// Keep every type POD/blittable: the GUI polls snapshots and pushes input
// events, and never shares a thread with the audio path.

#include <stdint.h>

#if defined(_WIN32)
#if defined(P2S_DLL_EXPORTS)
#define P2S_API __declspec(dllexport)
#else
#define P2S_API __declspec(dllimport)
#endif
#else
#define P2S_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const char *storageDir;   // nullptr = %APPDATA%\Pico2Seq
    int startAudioDevice;     // 0 = offline (tests), 1 = WASAPI device
    int startControlThread;   // 0 = GUI/test pumps p2s_update(), 1 = internal 1 kHz thread
} P2sInitOptions;

// Lifecycle -------------------------------------------------------------
P2S_API int p2s_init(const P2sInitOptions *options); // 0 on success
P2S_API void p2s_shutdown(void);
P2S_API int p2s_is_running(void);

// Transport --------------------------------------------------------------
P2S_API void p2s_transport_start(void);
P2S_API void p2s_transport_stop(void);
P2S_API int p2s_transport_running(void);

// Input events (GUI -> engine) ---------------------------------------------
// 32-pad touch bitmask (1 = touched, MPR121 electrode numbering). A change
// fires the touch-interrupt path exactly like the physical sensor.
P2S_API void p2s_push_touch(uint16_t electrodeBits);
// Fake lidar reading in millimetres; negative = no hand in view.
P2S_API void p2s_push_lidar(int distanceMm);

// Display snapshots (engine -> GUI) ----------------------------------------
// 32 LEDs * 3 bytes RGB; returns the frame serial (0 = never rendered).
P2S_API uint64_t p2s_poll_leds(uint8_t *outRgb96);
// 128x64 1bpp page buffer (1024 bytes); returns the frame serial.
P2S_API uint64_t p2s_poll_oled(uint8_t *outBuffer1024);

typedef struct
{
    int transportRunning;
    int selectedVoice;   // 0-3
    float tempoBpm;      // 45-200
    int currentScale;    // 0-12
    int currentShuffle;  // 0-15
    int currentTheme;    // 0-9
    int stepEditActive;  // 1 when a step is selected for edit
    int voiceEditorActive;
    int padBankB;        // 0 = pads address voices 0/1, 1 = voices 2/3
    uint32_t processedSteps;
    uint64_t ledFrames;
    uint64_t oledFrames;
} P2sStatus;

P2S_API void p2s_get_status(P2sStatus *out);

// Diagnostics (grows per phase; Phase 0 exposes step throughput) ---------
P2S_API uint32_t p2s_processed_step_count(void);

#ifdef __cplusplus
}
#endif

#endif // P2S_DESKTOP_API_H
