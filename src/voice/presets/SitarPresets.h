#pragma once

#include "../VoiceConfig.h"
#include "../VoiceParameters.h"

namespace VoicePresets {
  // Owned sitar lanes, centered on the preset's resting sound. All three are
  // linear percent controls; TARAF is a level, not a time, so the Decay slot
  // keeps the plain control shape instead of the waveguide T60 seconds taper.
  constexpr VoiceParameterLayout sitarStringLayout(VoiceParameters::Span jawari,
                                                   VoiceParameters::Span pick,
                                                   VoiceParameters::Span taraf) noexcept
  {
    using VoiceParameters::spanned;
    VoiceParameterLayout p = VoiceParameters::sitarLayout();
    p.slots[static_cast<size_t>(ParamId::Filter)] =
        spanned(p.slots[static_cast<size_t>(ParamId::Filter)], jawari, dspmap::Mapping::LINEAR);
    p.slots[static_cast<size_t>(ParamId::Attack)] =
        spanned(p.slots[static_cast<size_t>(ParamId::Attack)], pick, dspmap::Mapping::LINEAR);
    p.slots[static_cast<size_t>(ParamId::Decay)] =
        spanned(p.slots[static_cast<size_t>(ParamId::Decay)], taraf, dspmap::Mapping::LINEAR);
    return p;
  }
  //                                               JAWARI               PICK                 TARAF
  inline constexpr auto kSitarLayout = sitarStringLayout({0.05f, 0.45f, 0.95f}, {0.3f, 0.9f, 1.0f}, {0.0f, 0.45f, 0.9f});

  constexpr VoiceConfig makeSitar() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_SITAR;
    c.paramSet = PARAMSET_SITAR;
    c.parameters = &kSitarLayout;
    c.sitarDecay = 4.0f;           // long main-string tail: several seconds, not tens
    c.sitarBrightness = 0.85f;     // bright ringing top
    c.sitarPickPosition = 0.12f;   // bridge-side: thin, nasal attack
    c.sitarPickHardness = 0.9f;    // hard pick: full-bandwidth excitation
    c.sitarJawari = 0.45f;         // moderate bridge buzz
    c.sitarJawariThreshold = 0.3f; // contact engages at a moderate amplitude
    c.sitarTarafAmount = 0.45f;    // strong sympathetic bank, under the string
    c.sitarTarafDecay = 4.0f;      // tarabs keep ringing after the string dies
    c.sitarBodyAmount = 0.2f;      // light wooden body
    c.sitarBodyFrequency = 130.0f; // low gourd mode

    c.hasOverdrive = false;
    c.hasFilter = false;   // no conventional synth filter on the acoustic chain
    c.hasEnvelope = false; // natural physical-model decay instead of a gated VCA
    c.highPassFreq = 65.0f;  // sheds sub rumble below the body mode
    c.highPassRes = 0.0f;
    c.outputLevel = 0.7f;  // string + taraf + body sum hot
    return c;
  }
} // namespace VoicePresets
