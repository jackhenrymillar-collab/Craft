#pragma once

namespace foundry
{
struct CompiledInstrument;

// Sets the instrument's contact- and air-mic output gains by playing a short reference hit
// (a medium mallet at a fixed speed) offline and measuring the level. Different objects then
// come out at about the same loudness for the same hit, and the pad Level control starts from
// a sensible place. Runs on the compile thread.
void calibrateOutputLevels (CompiledInstrument& ci);

// RMS of the reference hit's first 40 ms. M3 lowered it 4 dB (from 0.0478) for kit headroom;
// the membrane's reference hit used to chatter (fixed in M3), which had held it 11 dB down.
inline constexpr double kCalibrationTarget = 0.03;
// Level of a 1 N brush stroke relative to the medium reference hit (about -6 dB; a full
// stroke ends up 10-15 dB under a medium hit, since the force is band-limited noise).
inline constexpr double kSweepLevel = 0.5;
// Ceiling for the hard reference hit's peak.
inline constexpr double kCalibrationPeak = 0.25;
}
