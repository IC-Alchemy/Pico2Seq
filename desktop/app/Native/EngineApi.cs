using System.Runtime.InteropServices;

namespace Pico2Seq_App.Native;

/// <summary>
/// P/Invoke surface of p2s_desktop.dll (desktop/native/src/api.h).
/// All structs must stay blittable and field-order-identical to the C ABI.
/// </summary>
public static class EngineApi
{
    private const string Dll = "p2s_desktop";

    [StructLayout(LayoutKind.Sequential)]
    public struct InitOptions
    {
        public IntPtr StorageDir;       // const char* (null = %APPDATA%\Pico2Seq)
        [MarshalAs(UnmanagedType.I1)] public bool StartAudioDevice;
        [MarshalAs(UnmanagedType.I1)] public bool StartControlThread;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Status
    {
        public int TransportRunning;
        public int SelectedVoice;
        public float TempoBpm;
        public int CurrentScale;
        public int CurrentShuffle;
        public int CurrentTheme;
        public int StepEditActive;
        public int VoiceEditorActive;
        public int PadBankB;
        public uint ProcessedSteps;
        public ulong LedFrames;
        public ulong OledFrames;
    }

    [DllImport(Dll)] public static extern int p2s_init(in InitOptions options);
    [DllImport(Dll)] public static extern void p2s_shutdown();
    [DllImport(Dll)] public static extern int p2s_is_running();

    [DllImport(Dll)] public static extern void p2s_transport_start();
    [DllImport(Dll)] public static extern void p2s_transport_stop();
    [DllImport(Dll)] public static extern int p2s_transport_running();

    [DllImport(Dll)] public static extern void p2s_push_touch(ushort electrodeBits);
    [DllImport(Dll)] public static extern void p2s_push_lidar(int distanceMm);

    [DllImport(Dll)] public static extern ulong p2s_poll_leds(byte[] outRgb96);
    [DllImport(Dll)] public static extern ulong p2s_poll_oled(byte[] outBuffer1024);
    [DllImport(Dll)] public static extern void p2s_get_status(ref Status status);

    [DllImport(Dll)] public static extern uint p2s_processed_step_count();

    public const int LedCount = 32;
    public const int LedBytes = LedCount * 3;
    public const int OledBytes = 128 * 64 / 8;
}
