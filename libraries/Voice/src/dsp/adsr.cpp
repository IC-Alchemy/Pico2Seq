#include "adsr.h"
#include <math.h>

using namespace daisysp;

void Adsr::Init(float sample_rate, int blockSize) {
  sample_rate_ = sample_rate / blockSize;
  attackShape_ = -1.f;
  attackTarget_ = 0.0f;
  attackTime_ = -1.f;
  decayTime_ = -1.f;
  releaseTime_ = -1.f;
  sus_level_ = 0.7f;
  x_ = 0.0f;
  gate_ = false;
  mode_ = ADSR_SEG_IDLE;

  SetTime(ADSR_SEG_ATTACK, 0.1f);
  SetTime(ADSR_SEG_DECAY, 0.1f);
  SetTime(ADSR_SEG_RELEASE, 0.1f);
}

void Adsr::Retrigger(bool hard) {
  mode_ = ADSR_SEG_ATTACK;
  if (hard)
    x_ = 0.f;
}

void Adsr::SetTime(int seg, float time) {
  switch (seg) {
  case ADSR_SEG_ATTACK:
    SetAttackTime(time, 0.0f);
    break;
  case ADSR_SEG_DECAY: {
    SetTimeConstant(time, decayTime_, decayD0_);
  } break;
  case ADSR_SEG_RELEASE: {
    SetTimeConstant(time, releaseTime_, releaseD0_);
  } break;
  default:
    return;
  }
}

void Adsr::SetAttackTime(float timeInS, float shape) {
  if ((timeInS != attackTime_) || (shape != attackShape_)) {
    attackTime_ = timeInS;
    attackShape_ = shape;
    if (timeInS > 0.f) {
      float x = shape;
      float target = 9.f * powf(x, 10.f) + 0.3f * x + 1.01f;
      attackTarget_ = target;
      float logTarget = logf(1.f - (1.f / target)); // -1 for decay
      attackD0_ = 1.f - expf(logTarget / (timeInS * sample_rate_));
    } else
      attackD0_ = 1.f; // instant change
  }
}
void Adsr::SetDecayTime(float timeInS) {
  SetTimeConstant(timeInS, decayTime_, decayD0_);
}
void Adsr::SetReleaseTime(float timeInS) {
  SetTimeConstant(timeInS, releaseTime_, releaseD0_);
}

void Adsr::SetTimeConstant(float timeInS, float &time, float &coeff) {
  if (timeInS != time) {
    time = timeInS;
    if (time > 0.f) {
      const float target = logf(1. / M_E);
      coeff = 1.f - expf(target / (time * sample_rate_));
    } else
      coeff = 1.f; // instant change
  }
}
float Adsr::Process(bool gate) {
  float out = 0.0f;

  // Handle gate changes
  if (gate && !gate_) // Rising edge: start attack
    mode_ = ADSR_SEG_ATTACK;
  else if(!gate && gate_) // Falling edge: start release
     mode_ = ADSR_SEG_RELEASE;
  gate_ = gate;

  // Select appropriate coefficient based on current mode
  float D0(attackD0_);
  if (mode_ == ADSR_SEG_DECAY)
    D0 = decayD0_;
  else if (mode_ == ADSR_SEG_RELEASE)
    D0 = releaseD0_;

  // Set target level based on current mode
  float target = -0.01f; // Default target for release
  if (mode_ == ADSR_SEG_DECAY)
    target = sus_level_;
  else if (mode_ == ADSR_SEG_SUSTAIN)
    target = sus_level_;

  // Process ADSR based on current mode
  switch (mode_) {
  case ADSR_SEG_IDLE:
    // Envelope is idle, output is 0
    out = 0.0f;
    break;
  case ADSR_SEG_ATTACK:
    // Apply attack curve
    x_ += D0 * (attackTarget_ - x_);
    out = x_;
    if (out > 1.f) {
      // If output exceeds 1, clamp it to 1 and move to decay stage
      x_ = out = 1.f;
      mode_ = ADSR_SEG_DECAY; // Move to decay stage
    }
    break;
  case ADSR_SEG_DECAY:
    // Apply decay curve
    x_ += D0 * (sus_level_ - x_);
    out = x_;
    if (fabs(x_ - sus_level_) < 0.0001f) {
      // If output is close enough to sustain level, move to sustain stage
      mode_ = ADSR_SEG_SUSTAIN;
    }
    break;
  case ADSR_SEG_SUSTAIN:
    // Hold at sustain level until gate goes off
    out = sus_level_;
    break;
  case ADSR_SEG_RELEASE:
    // Apply decay or release curve
    x_ += D0 * (target - x_);
    out = x_;
    if (out < 0.0f) {
      // If output falls below 0, clamp it to 0 and return to idle state
      x_ = out = 0.f;
      mode_ = ADSR_SEG_IDLE; // Return to idle state
    }
    break;
  default:
    // Default case, should not be reached
    break;
  }
  return out;
}