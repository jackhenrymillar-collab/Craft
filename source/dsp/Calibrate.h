#pragma once

namespace foundry
{
struct CompiledInstrument;

// Sets the instrument's contact- and air-mic output gains by playing a short reference hit
// (a medium mallet at a fixed speed) offline and measuring the level. Different objects then
// come out at about the same loudness for the same hit, and the pad Level control starts from
// a sensible place. Runs on the compile thread.
void calibrateOutputLevels (CompiledInstrument& ci);

// RMS of the reference hit's first 40 ms that maps to the M1 drum's level (kept so the
// default drum sounds exactly as it did).
inline constexpr double kCalibrationTarget = 0.0478;
// Ceiling for the hard reference hit's peak.
inline constexpr double kCalibrationPeak = 0.25;
}
