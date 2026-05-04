#include "includes.h"
#include "diagnostic.h"

// =======================
//   TEMPLATE SETTINGS
// =======================
constexpr uint8_t kSequencerCount = 4;
constexpr uint8_t kMidiChannel = 1;
constexpr uint8_t kBaseMidiNote = 36;
constexpr uint8_t kDefaultTempoBpm = 90;
constexpr uint32_t kSerialBaud = 115200;
constexpr int kMidiNoteMin = 0;
constexpr int kMidiNoteMax = 127;

// =======================
//   GLOBAL STATE
// =======================
Adafruit_USBD_MIDI rawUsbMidi;
midi::SerialMIDI<Adafruit_USBD_MIDI> serialUsbMidi(rawUsbMidi);
midi::MidiInterface<midi::SerialMIDI<Adafruit_USBD_MIDI>> usbMidi(serialUsbMidi);

Sequencer sequencers[kSequencerCount] = {
    Sequencer(1),
    Sequencer(2),
    Sequencer(3),
    Sequencer(4),
};

VoiceState sequencerStates[kSequencerCount];
int8_t activeMidiNotes[kSequencerCount] = {-1, -1, -1, -1};
bool isClockRunning = false;
uint32_t ppqnTicksPending = 0;
uint8_t currentScale = 0;

// =======================
//   HELPERS
// =======================
uint8_t clampSequencerIndex(uint8_t oneBasedIndex)
{
    if (oneBasedIndex == 0) {
        return 0;
    }
    if (oneBasedIndex > kSequencerCount) {
        return kSequencerCount - 1;
    }
    return oneBasedIndex - 1;
}

uint8_t clampStepIndex(uint8_t oneBasedStep)
{
    if (oneBasedStep == 0) {
        return 0;
    }
    if (oneBasedStep > SequencerConstants::MAX_STEPS_COUNT) {
        return SequencerConstants::MAX_STEPS_COUNT - 1;
    }
    return oneBasedStep - 1;
}

int clampMidiNote(int note)
{
    if (note < kMidiNoteMin) {
        return kMidiNoteMin;
    }
    if (note > kMidiNoteMax) {
        return kMidiNoteMax;
    }
    return note;
}

int mapVoiceStateToMidiNote(const VoiceState &state)
{
    const uint8_t noteIndex = static_cast<uint8_t>(
        constrain(state.noteIndex, 0.0f, static_cast<float>(SCALE_STEPS - 1)));
    const uint8_t scaleIndex = currentScale % SCALES_COUNT;
    return clampMidiNote(scale[scaleIndex][noteIndex] + kBaseMidiNote + static_cast<int>(state.octaveOffset));
}

uint8_t mapVelocity(const VoiceState &state)
{
    return static_cast<uint8_t>(constrain(state.velocityLevel, 0.0f, 1.0f) * 127.0f);
}

void stopActiveMidiNote(uint8_t sequencerIndex)
{
    if (sequencerIndex >= kSequencerCount || activeMidiNotes[sequencerIndex] < 0) {
        return;
    }

    usbMidi.sendNoteOff(static_cast<uint8_t>(activeMidiNotes[sequencerIndex]), 0, kMidiChannel);
    activeMidiNotes[sequencerIndex] = -1;
}

void onSequencerNoteOff(uint8_t note, uint8_t channel)
{
    (void)note;
    const uint8_t sequencerIndex = channel > 0 ? channel - 1 : 0;
    stopActiveMidiNote(sequencerIndex);
}

void startOrRetriggerMidiNote(uint8_t sequencerIndex, const VoiceState &state)
{
    if (sequencerIndex >= kSequencerCount || !state.isGateHigh || !state.shouldRetrigger) {
        return;
    }

    stopActiveMidiNote(sequencerIndex);
    const int midiNote = mapVoiceStateToMidiNote(state);
    usbMidi.sendNoteOn(static_cast<uint8_t>(midiNote), mapVelocity(state), kMidiChannel);
    activeMidiNotes[sequencerIndex] = static_cast<int8_t>(midiNote);
}

ParamId parseParameterToken(char token)
{
    switch (tolower(token)) {
        case 'n': return ParamId::Note;
        case 'v': return ParamId::Velocity;
        case 'f': return ParamId::Filter;
        case 'a': return ParamId::Attack;
        case 'd': return ParamId::Decay;
        case 'o': return ParamId::Octave;
        case 'l': return ParamId::GateLength;
        case 'g': return ParamId::Gate;
        case 's': return ParamId::Slide;
        default: return ParamId::Gate;
    }
}

void printHelp()
{
    Serial.println("Pico2Seq Template");
    Serial.println("Commands:");
    Serial.println("  p                      toggle play/stop");
    Serial.println("  t <seq> <step>         toggle gate, 1-based");
    Serial.println("  x <seq> <step> <param> <value>");
    Serial.println("                         set parameter: n v f a d o l g s");
    Serial.println("  m <seq> <param> <steps>");
    Serial.println("                         set track length, 2-64 steps");
    Serial.println("  r <seq>                randomize one sequencer");
    Serial.println("  b <tempo>              set BPM");
    Serial.println("  c <scale>              set scale index, 0-based");
}

bool readSerialLine(char *buffer, size_t bufferSize)
{
    static char line[80];
    static size_t index = 0;

    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            line[index] = '\0';
            strncpy(buffer, line, bufferSize);
            buffer[bufferSize - 1] = '\0';
            index = 0;
            return true;
        }
        if (index < sizeof(line) - 1) {
            line[index++] = c;
        }
    }

    return false;
}

void pollSerialInput(TemplateInputEvent &event)
{
    char line[80];
    if (!readSerialLine(line, sizeof(line))) {
        return;
    }

    char *command = strtok(line, " ");
    if (!command || command[0] == '\0') {
        return;
    }

    switch (tolower(command[0])) {
        case '?':
        case 'h':
            printHelp();
            break;
        case 'p':
            event.action = TemplateInputAction::ToggleRun;
            break;
        case 't': {
            char *seq = strtok(nullptr, " ");
            char *step = strtok(nullptr, " ");
            if (seq && step) {
                event.action = TemplateInputAction::ToggleGate;
                event.sequencerIndex = clampSequencerIndex(static_cast<uint8_t>(atoi(seq)));
                event.stepIndex = clampStepIndex(static_cast<uint8_t>(atoi(step)));
            }
            break;
        }
        case 'x': {
            char *seq = strtok(nullptr, " ");
            char *step = strtok(nullptr, " ");
            char *param = strtok(nullptr, " ");
            char *value = strtok(nullptr, " ");
            if (seq && step && param && value) {
                event.action = TemplateInputAction::SetParameter;
                event.sequencerIndex = clampSequencerIndex(static_cast<uint8_t>(atoi(seq)));
                event.stepIndex = clampStepIndex(static_cast<uint8_t>(atoi(step)));
                event.parameter = parseParameterToken(param[0]);
                event.value = atof(value);
            }
            break;
        }
        case 'm': {
            char *seq = strtok(nullptr, " ");
            char *param = strtok(nullptr, " ");
            char *steps = strtok(nullptr, " ");
            if (seq && param && steps) {
                event.action = TemplateInputAction::SetTrackLength;
                event.sequencerIndex = clampSequencerIndex(static_cast<uint8_t>(atoi(seq)));
                event.parameter = parseParameterToken(param[0]);
                event.value = atof(steps);
            }
            break;
        }
        case 'r': {
            char *seq = strtok(nullptr, " ");
            if (seq) {
                event.action = TemplateInputAction::Randomize;
                event.sequencerIndex = clampSequencerIndex(static_cast<uint8_t>(atoi(seq)));
            }
            break;
        }
        case 'b': {
            char *tempo = strtok(nullptr, " ");
            if (tempo) {
                event.action = TemplateInputAction::SetTempo;
                event.value = atof(tempo);
            }
            break;
        }
        case 'c': {
            char *scaleIndex = strtok(nullptr, " ");
            if (scaleIndex) {
                event.action = TemplateInputAction::SetScale;
                event.value = atof(scaleIndex);
            }
            break;
        }
        default:
            Serial.println("Unknown command. Type h for help.");
            break;
    }
}

void toggleClock()
{
    if (isClockRunning) {
        uClock.stop();
        return;
    }

    uClock.start();
}

void processInputEvent(const TemplateInputEvent &event)
{
    if (event.action == TemplateInputAction::None) {
        return;
    }

    Sequencer &sequencer = sequencers[event.sequencerIndex % kSequencerCount];

    switch (event.action) {
        case TemplateInputAction::ToggleRun:
            toggleClock();
            break;
        case TemplateInputAction::ToggleGate:
            sequencer.toggleStep(event.stepIndex);
            break;
        case TemplateInputAction::SetParameter:
            sequencer.setStepParameterValue(event.parameter, event.stepIndex, event.value);
            break;
        case TemplateInputAction::SetTrackLength:
            sequencer.setParameterStepCount(event.parameter, static_cast<uint8_t>(event.value));
            break;
        case TemplateInputAction::Randomize:
            sequencer.randomizeParameters();
            break;
        case TemplateInputAction::SetTempo:
            uClock.setTempo(constrain(event.value, 20.0f, 300.0f));
            break;
        case TemplateInputAction::SetScale:
            currentScale = static_cast<uint8_t>(event.value) % SCALES_COUNT;
            break;
        case TemplateInputAction::None:
            break;
    }
}

void pollInputs()
{
    TemplateInputEvent event;
    pollSerialInput(event);
    processInputEvent(event);

    event = TemplateInputEvent{};
    readHardwareInput(event);
    processInputEvent(event);
}

// =======================
//   CLOCK CALLBACKS
// =======================
void onSync24Callback(uint32_t tick)
{
    (void)tick;
    usbMidi.sendRealTime(midi::Clock);
}

void onClockStart()
{
    isClockRunning = true;
    usbMidi.sendRealTime(midi::Start);
    for (Sequencer &sequencer : sequencers) {
        sequencer.start();
    }
}

void onClockStop()
{
    isClockRunning = false;
    usbMidi.sendRealTime(midi::Stop);
    for (uint8_t i = 0; i < kSequencerCount; ++i) {
        sequencers[i].stop();
        sequencers[i].reset();
        stopActiveMidiNote(i);
    }
}

void onOutputPPQNCallback(uint32_t tick)
{
    (void)tick;
    ppqnTicksPending++;
}

void onStepCallback(uint32_t uClockCurrentStep)
{
    for (uint8_t i = 0; i < kSequencerCount; ++i) {
        ParameterInputState input;
        sequencers[i].advanceStep(static_cast<uint8_t>(uClockCurrentStep), input, &sequencerStates[i]);
        startOrRetriggerMidiNote(i, sequencerStates[i]);
    }
}

void processPendingClockTicks()
{
    while (ppqnTicksPending > 0) {
        ppqnTicksPending--;
        for (uint8_t i = 0; i < kSequencerCount; ++i) {
            sequencers[i].tickNoteDuration(&sequencerStates[i]);
        }
    }
}

// =======================
//   ARDUINO ENTRY POINTS
// =======================
void setup()
{
    Serial.begin(kSerialBaud);
    usbMidi.begin(MIDI_CHANNEL_OMNI);

    for (Sequencer &sequencer : sequencers) {
        sequencer.initializeParameters();
        sequencer.resetAllSteps();
        sequencer.setMidiNoteOffCallback(onSequencerNoteOff);
    }

    // A small useful default pattern on sequencer 1.
    sequencers[0].setStepParameterValue(ParamId::Gate, 0, 1.0f);
    sequencers[0].setStepParameterValue(ParamId::Gate, 4, 1.0f);
    sequencers[0].setStepParameterValue(ParamId::Gate, 8, 1.0f);
    sequencers[0].setStepParameterValue(ParamId::Gate, 12, 1.0f);
    sequencers[0].setStepParameterValue(ParamId::Note, 0, 0.0f);
    sequencers[0].setStepParameterValue(ParamId::Note, 4, 2.0f);
    sequencers[0].setStepParameterValue(ParamId::Note, 8, 4.0f);
    sequencers[0].setStepParameterValue(ParamId::Note, 12, 7.0f);

    uClock.init();
    uClock.setOnSync24(onSync24Callback);
    uClock.setOnClockStart(onClockStart);
    uClock.setOnClockStop(onClockStop);
    uClock.setOutputPPQN(uClock.PPQN_480);
    uClock.setOnStep(onStepCallback);
    uClock.setOnOutputPPQN(onOutputPPQNCallback);
    uClock.setTempo(kDefaultTempoBpm);

    printHelp();
}

void loop()
{
    usbMidi.read();
    pollInputs();
    processPendingClockTicks();
}
