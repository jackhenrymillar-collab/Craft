#pragma once

#include "Materials.h"
#include "Treatments.h"

namespace foundry
{

// M1 instrument description: one circular membrane, one mallet, one contact mic.
// This becomes a ValueTree-backed graph in M2+; the compile step reads this plain
// struct so it can run on any thread.

struct MembraneSpec
{
    double radius = 0.178;     // m (14" head)
    double tension = 2000.0;   // N/m
    MaterialId material = MaterialId::Mylar;
    double highFreqDamping = 0.2; // extra loss per kHz (1 + h * f/1kHz)
};

struct MalletSpec
{
    double hardness = 0.5;     // 0 felt .. 1 hard plastic/wood tip
    double mass = 0.012;       // kg, effective stick/beater mass
};

struct PointPolar
{
    double r = 0.0;            // 0 centre .. 1 rim (fraction of radius)
    double theta = 0.0;        // radians
};

struct InstrumentSpec
{
    MembraneSpec membrane;
    MalletSpec mallet;
    PointPolar strike { 0.3, 0.0 };
    double strikeSpread = 0.05;    // random radial/angle jitter per hit (fraction of radius)
    PointPolar contactMic { 0.82, 1.1 };
    double tuneSemitones = 0.0;    // Size/Tune: scales the whole object
    int quality = 96;              // max number of exactly-solved modes
    int highBandModes = 96;        // statistical modes covering the dense top end
    Treatments treatments;
};

} // namespace foundry
