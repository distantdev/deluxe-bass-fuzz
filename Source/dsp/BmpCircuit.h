#pragma once

#include "NonlinearCircuit.h"

//==============================================================================
/**
    Component values of the modelled Deluxe Bass Fuzz core and helpers shared by
    the stage models. Values and sources: docs/deluxe_bass_fuzz.cir (Green/Black
    classic Classic Bass Fuzz BOMs: standard component values; circuit reference / circuit reference notes;
    tone-stack R_LP = 11.5k per circuit reference for the Deluxe Bass Fuzz).
*/
namespace bmd
{
    namespace values
    {
        constexpr double Vcc = 9.0;

        // Q1 input booster (R1 series, R2 bias, R3 || C2 feedback, R5 collector, R4 emitter)
        constexpr double Q1_Cin = 100e-9, Q1_Rs = 39e3, Q1_Rb = 100e3, Q1_Rf = 470e3, Q1_Cf = 470e-12;
        constexpr double Q1_Rc = 12e3, Q1_Re = 390.0;

        // SUSTAIN: C3 -> 100k log pot -> R7 1k to ground (R7 placement is a guess)
        constexpr double Sus_C = 100e-9, Sus_Pot = 100e3, Sus_R7 = 1e3;

        // Q2/Q3 clipping stages (10k series, 100k bias, 470k || 470p || (47n + 1N914 pair))
        constexpr double Clip_Cin = 100e-9, Clip_Rs = 10e3, Clip_Rb = 100e3, Clip_Rf = 470e3, Clip_Cf = 470e-12;
        constexpr double Clip_Cd = 47e-9, Clip_Rc = 12e3, Clip_Re = 390.0;

        // Tone stack (R_LP 11.5k circuit reference BBM; C_LP 10n; C_HP 3n9; R_HP 22k; 100k lin pot)
        constexpr double Tone_Rlp = 11.5e3, Tone_Clp = 10e-9, Tone_Chp = 3.9e-9, Tone_Rhp = 22e3, Tone_Pot = 100e3;

        // Q4 recovery stage (C12 coupling, R21 bias, R22 feedback, R24 collector, R23 emitter)
        constexpr double Q4_Cin = 100e-9, Q4_Rb = 100e3, Q4_Rf = 470e3, Q4_Rc = 10e3, Q4_Re = 2e3;

        // Output coupling into the 100k volume pot (modelled at max: 100k to ground)
        constexpr double Out_C = 100e-9, Out_Rvol = 100e3;

        /** 100k log (audio) pot law used by the netlist: wiper fraction for knob 0..1 (~9% at noon). */
        inline double logTaper (double knob)
        {
            knob = std::min (1.0, std::max (0.0, knob));
            return (std::pow (10.0, 2.0 * knob) - 1.0) / 99.0;
        }
    }

    /** DC bias + small-signal parameters of a Classic Bass Fuzz common-emitter stage. */
    inline BjtSmallSignal biasCommonEmitter (double rc, double rf, double rb, double re)
    {
        NonlinearCircuit<3, 0, 1, 0> c;         // 0 base, 1 collector, 2 emitter
        c.clear();
        c.addResistor (1, 0, rf);
        c.addResistor (0, -1, rb);
        c.addResistor (2, -1, re);
        c.addSupply (1, rc, values::Vcc);
        c.setBjt (0, 1, 2);
        c.prepare (48000.0);
        return c.getBjtSmallSignal();
    }

    /** Copy of a downstream CE stage's input, attached at node 'drive', used as
        this stage's load. The transistor is linearised at its bias point; if cd > 0
        the stage's clipping diode pair (in series with cd, collector -> base) is
        included and stays nonlinear, so the load's input impedance drops when the
        downstream diodes conduct (as in the real circuit).
        Uses 4 nodes starting at n0 (coupling node, base, collector, emitter), plus
        one more (n0 + 4, diode node) if cd > 0.
        rcAc is the stage's AC collector load (its collector resistor || its own load). */
    template <typename Circuit>
    inline void addStageReplica (Circuit& c, int drive, int n0,
                                 double cin, double rs, double rb, double rf, double cf,
                                 double rcAc, double re, const BjtSmallSignal& s, double cd = 0.0)
    {
        const int nc = n0, b = n0 + 1, col = n0 + 2, e = n0 + 3;
        if (cd > 0.0)
        {
            c.addCapacitor (col, n0 + 4, cd);
            c.addDiodePair (n0 + 4, b);
        }
        if (rs > 0.0)
        {
            c.addCapacitor (drive, nc, cin);
            c.addResistor (nc, b, rs);
        }
        else
        {
            c.addCapacitor (drive, b, cin);
            c.addResistor (nc, -1, 1.0e6);      // unused node, keep the matrix regular
        }
        c.addResistor (b, -1, rb);
        c.addResistor (col, b, rf);
        if (cf > 0.0)
            c.addCapacitor (col, b, cf);
        c.addResistor (col, -1, rcAc);
        c.addResistor (e, -1, re);
        c.addLinearBjt (b, col, e, s);
    }

    inline double parallel (double a, double b) { return a * b / (a + b); }
}
