#pragma once

#include <algorithm>
#include <cmath>

//==============================================================================
/**
    Tiny fixed-topology circuit simulator for one Classic Bass Fuzz gain stage.

    Elements: resistors, capacitors (trapezoidal companion models), VCCS (used
    for the linearised copy of the next stage that loads this one), DC supplies
    (Norton: resistor to a fixed voltage), the input source (Norton, 1 ohm),
    NB = 0/1 BJT (Ebers-Moll transport model with the Gummel-Poon Early factor
    (1 - Vbc/Vaf), i.e. the 2N5088 SPICE model without charge storage, high
    injection and ohmic resistances) and ND anti-parallel diode pairs (1N914).

    Build time (prepare(), or update() after a pot moved): modified nodal
    analysis, matrix inversion, DC operating point (capacitors open), and the
    reduction of the circuit to a discrete state-space form whose state is the
    NC capacitor history currents h (the "DK method"):

        u0 = Au h + bu vs + cu                 junction voltages without junction currents
        u  = u0 + K j(u)                       Newton on the M = 2 NB + ND junction voltages
        vc = Ac h + bc vs + cc + Bc j          capacitor voltages
        y  = ay h + by vs + cy + dy j          output node voltage
        h <- 2 Gc vc - h                       trapezoidal state update

    Per sample that is ~NC (NC + M + P + 1) multiply-adds plus a small Newton
    solve (SPICE-style pn-junction step limiting, warm-started from a linear
    prediction, typically 1-2 iterations at 192 kHz). All sizes are template
    parameters so the inner loops are fixed-length. No allocation.

    All voltages are absolute (bias included); callers subtract getDcOutput().
*/
namespace bmd
{
    constexpr double kThermalVoltage = 0.025865;   // kT/q at 27 degC (SPICE TNOM)
    constexpr double kMaxInputVoltage = 100.0;      // source clamp in NonlinearCircuit::process()


    /** 2N5088 (LTspice standard.bjt) */
    struct BjtModel
    {
        double Is = 5.911e-15, Bf = 1122.0, Br = 1.271, Ise = 5.911e-15, Ne = 1.394, Vaf = 62.37;
    };

    /** 1N914 / 1N4148 (LTspice standard.dio) */
    struct DiodeModel
    {
        double Is = 2.52e-9, N = 1.752;
    };

    /** Small-signal parameters of a biased BJT (for linearised load replicas). */
    struct BjtSmallSignal
    {
        double ic = 0.0, gm = 0.0, gpi = 0.0, gmu = 0.0;   // gmu: Early term dIc/dVcb
        double vc = 0.0, vb = 0.0, ve = 0.0;
    };

    template <int N, int NC, int NB, int ND>
    class NonlinearCircuit
    {
    public:
        static constexpr int Gnd = -1;
        static constexpr int M = 2 * NB + ND;   // junction voltages
        static constexpr int P = 3 * NB + ND;   // junction currents

        void clear()
        {
            nRes = nCap = nVccs = numDp = 0;
            hasBjt = false;
            inNode = Gnd; inG = 0.0; outNode = 0;
            std::fill (std::begin (supplyG), std::end (supplyG), 0.0);
            std::fill (std::begin (supplyI), std::end (supplyI), 0.0);
        }

        int  addResistor (int a, int b, double r)     { res[nRes] = { a, b, 1.0 / std::max (r, 1.0e-3) }; return nRes++; }   // nRes < MaxR (40)
        void setResistor (int idx, double r)          { res[idx].g = 1.0 / std::max (r, 1.0e-3); dirty = true; }
        void addConductance (int a, int b, double g)  { res[nRes++] = { a, b, g }; }
        void addCapacitor (int a, int b, double c)    { if (nCap < NC) caps[nCap] = { a, b, c }; ++nCap; }
        void addVccs (int op, int on, int cp, int cn, double gm) { vccs[nVccs++] = { op, on, cp, cn, gm }; }
        void addSupply (int node, double r, double v) { supplyG[node] += 1.0 / r; supplyI[node] += v / r; }
        void setInput (int node, double r)            { inNode = node; inG = 1.0 / r; }
        void setOutput (int node)                     { outNode = node; }
        void setBjt (int b, int c, int e, const BjtModel& m = {}) { hasBjt = true; bjtB = b; bjtC = c; bjtE = e; bjt = m; }
        void addDiodePair (int a, int b, const DiodeModel& m = {}) { if (numDp < ND) dps[numDp] = { a, b, m, 0, 0, 0.6 }; ++numDp; }

        //==========================================================================
        /** Build matrices, solve the DC operating point, initialise all state.
            Returns false if the topology does not match the template sizes or
            the DC solve failed. */
        bool prepare (double sampleRate)
        {
            if (nCap != NC || numDp != ND || hasBjt != (NB == 1))
                return false;

            fs = sampleRate;
            setupNonlinear();
            for (int k = 0; k < NC; ++k)
                gc[k] = 2.0 * caps[k].c * fs;

            // ---- DC operating point (capacitors open), full base-current model
            effectiveBeta = 0.0;
            double Z[N][N];
            buildAndInvert (true, Z);
            const bool ok = solveOperatingPoint (Z);

            // For the transient, the two-exponential base current (ideal Is/Bf plus
            // non-ideal Ise/Ne) is replaced by one ideal exponential scaled to the
            // same base current at the operating point (beta_eff ~ 550 vs Bf = 1122):
            // same bias and Ic law, one exp() less per Newton iteration.
            if (NB == 1)
            {
                const double vt = kThermalVoltage;
                const double ube = uDc[0];
                const double ib = bjt.Is / bjt.Bf * (std::exp (ube / vt) - 1.0)
                                + bjt.Ise * (std::exp (ube / (bjt.Ne * vt)) - 1.0);
                effectiveBeta = ib > 0.0 ? bjt.Is * (std::exp (ube / vt) - 1.0) / ib : bjt.Bf;
            }

            // ---- transient state-space matrices
            buildStateSpace();
            dirty = false;
            reset();
            return ok;
        }

        /** Rebuild the state-space matrices after a pot moved (keeps all state). */
        void update()
        {
            if (dirty)
            {
                buildStateSpace();
                dirty = false;
            }
        }

        /** Return to the DC operating point. */
        void reset()
        {
            for (int k = 0; k < NC; ++k)
                h[k] = gc[k] * vcDc[k];             // capacitor current 0
            for (int m = 0; m < M; ++m)
                u[m] = uPrev[m] = uDc[m];
            y = yDc;
            lastIterations = 0;
        }

        /** One sample with input source voltage vs; returns the output node voltage. */
        double process (double vs)
        {
            // Non-finite input is treated as silence; the source is limited to +-100 V
            // (far beyond any instrument or line level) so nothing can overflow.
            if (! std::isfinite (vs))
                vs = 0.0;
            vs = std::clamp (vs, -kMaxInputVoltage, kMaxInputVoltage);

            double j[P > 0 ? P : 1] {};
            if constexpr (M > 0)
            {
                double u0[M];
                for (int m = 0; m < M; ++m)
                {
                    double s = cu[m] + bu[m] * vs;
                    for (int k = 0; k < NC; ++k)
                        s += Au[m][k] * h[k];
                    u0[m] = s;
                }

                // linear predictor from the last two solutions, then Newton
                for (int m = 0; m < M; ++m)
                {
                    const double g = 2.0 * u[m] - uPrev[m];
                    uPrev[m] = u[m];
                    u[m] = g;
                }
                newton (u0, u, maxIterTransient, j, toleranceTransient);

                bool finite = true;
                for (int m = 0; m < M; ++m)
                    finite = finite && std::isfinite (u[m]);
                if (! finite)
                {
                    reset();
                    return y;
                }
            }

            double out = cy + by * vs;
            for (int k = 0; k < NC; ++k)
                out += ay[k] * h[k];
            for (int p = 0; p < P; ++p)
                out += dy[p] * j[p];

            double hn[NC];
            for (int k = 0; k < NC; ++k)
            {
                double vc = cc[k] + bc[k] * vs;
                for (int l = 0; l < NC; ++l)
                    vc += Ac[k][l] * h[l];
                for (int p = 0; p < P; ++p)
                    vc += Bc[k][p] * j[p];
                hn[k] = 2.0 * gc[k] * vc - h[k];
            }
            bool finite = std::isfinite (out);
            for (int k = 0; k < NC; ++k)
                finite = finite && std::isfinite (hn[k]);
            if (! finite)
            {
                reset();
                return y;
            }
            for (int k = 0; k < NC; ++k)
                h[k] = hn[k];

            y = out;
            return out;
        }

        double getOutput() const noexcept             { return y; }
        double getDcOutput() const noexcept           { return yDc; }
        double getDcVoltage (int node) const noexcept { return node < 0 ? 0.0 : vDc[node]; }
        int getLastIterationCount() const noexcept    { return lastIterations; }

        /** Small-signal parameters of the BJT at the DC operating point. */
        BjtSmallSignal getBjtSmallSignal() const
        {
            BjtSmallSignal s;
            if (NB == 0)
                return s;
            const double vt = kThermalVoltage;
            const double ube = uDc[0], ubc = uDc[1];
            const double ebe = std::exp (ube / vt), ebc = std::exp (ubc / vt);
            const double q = 1.0 - ubc / bjt.Vaf;
            s.ic = bjt.Is * (ebe - ebc) * q - bjt.Is / bjt.Br * (ebc - 1.0);
            s.gm = bjt.Is * ebe / vt * q;
            s.gpi = bjt.Is / bjt.Bf * ebe / vt + bjt.Ise * std::exp (ube / (bjt.Ne * vt)) / (bjt.Ne * vt);
            s.gmu = bjt.Is * (ebe - ebc) / bjt.Vaf;
            s.vb = getDcVoltage (bjtB); s.vc = getDcVoltage (bjtC); s.ve = getDcVoltage (bjtE);
            return s;
        }

        /** Stamp a linearised BJT (b, c, e) into this circuit (for load replicas). */
        void addLinearBjt (int b, int c, int e, const BjtSmallSignal& s)
        {
            addConductance (b, e, s.gpi);
            addVccs (c, e, b, e, s.gm);     // gm * vbe, collector -> emitter
            addVccs (c, e, c, b, s.gmu);    // Early: dIc/dVcb
        }

    private:
        static constexpr int MaxR = 40, MaxS = 8;
        static constexpr int MP = M > 0 ? M : 1, PP = P > 0 ? P : 1;
        static constexpr int maxIterTransient = 60, maxIterDc = 400;
        static constexpr double gmin = 1.0e-12;
        // Newton stops once a step is < 20 uV: the remaining junction-voltage error is then
        // ~ step^2 / (2 n Vt) < 1e-8 V (quadratic convergence), i.e. < -150 dB re 0.5 V.
        static constexpr double toleranceTransient = 2.0e-5;
        static constexpr double toleranceDc = 1.0e-12;

        struct Res  { int a, b; double g; };
        struct Cap  { int a, b; double c; };
        struct Vccs { int op, on, cp, cn; double gm; };
        struct DiodePair { int a, b; DiodeModel m; int ctrl, inj; double vcrit; };

        static double safeExp (double x, double& d) noexcept
        {
            constexpr double lim = 80.0;
            if (x > lim)
            {
                const double e = std::exp (lim);
                d = e;
                return e * (1.0 + x - lim);
            }
            d = std::exp (x);
            return d;
        }

        static double pnjlim (double vnew, double vold, double vt, double vcrit) noexcept
        {
            if (vnew > vcrit && std::abs (vnew - vold) > 2.0 * vt)
            {
                if (vold > 0.0)
                {
                    const double arg = 1.0 + (vnew - vold) / vt;
                    vnew = arg > 0.0 ? vold + vt * std::log (arg) : vcrit;
                }
                else
                {
                    vnew = vt * std::log (vnew / vt);
                }
            }
            return vnew;
        }

        void setupNonlinear()
        {
            int nc = 0, ni = 0;
            if (NB == 1)
            {
                ctrlP[0] = bjtB; ctrlM[0] = bjtE;          // u0 = Vbe
                ctrlP[1] = bjtB; ctrlM[1] = bjtC;          // u1 = Vbc
                injFrom[0] = bjtC; injTo[0] = bjtE;        // j0: transport current c -> e
                injFrom[1] = bjtB; injTo[1] = bjtE;        // j1: base-emitter diode b -> e
                injFrom[2] = bjtB; injTo[2] = bjtC;        // j2: base-collector diode b -> c
                nc = 2; ni = 3;
                const double vt = kThermalVoltage;
                vcritBjt = vt * std::log (vt / (std::sqrt (2.0) * bjt.Is));
            }
            for (int d = 0; d < ND; ++d)
            {
                auto& dp = dps[d];
                dp.ctrl = nc; dp.inj = ni;
                ctrlP[nc] = dp.a; ctrlM[nc] = dp.b; ++nc;
                injFrom[ni] = dp.a; injTo[ni] = dp.b; ++ni;
                const double nvt = dp.m.N * kThermalVoltage;
                dp.vcrit = nvt * std::log (nvt / (std::sqrt (2.0) * dp.m.Is));
            }
        }

        static double at (const double* v, int node) noexcept { return node < 0 ? 0.0 : v[node]; }

        void buildAndInvert (bool dc, double (&Z)[N][N]) const
        {
            double G[N][N] {};
            auto stamp = [&G] (int a, int b, double g)
            {
                if (a >= 0) G[a][a] += g;
                if (b >= 0) G[b][b] += g;
                if (a >= 0 && b >= 0) { G[a][b] -= g; G[b][a] -= g; }
            };

            for (int k = 0; k < nRes; ++k)
                stamp (res[k].a, res[k].b, res[k].g);
            if (! dc)
                for (int k = 0; k < NC; ++k)
                    stamp (caps[k].a, caps[k].b, gc[k]);
            for (int k = 0; k < nVccs; ++k)
            {
                const auto& s = vccs[k];
                auto add = [&G] (int r, int c, double g) { if (r >= 0 && c >= 0) G[r][c] += g; };
                add (s.op, s.cp, s.gm); add (s.op, s.cn, -s.gm);
                add (s.on, s.cp, -s.gm); add (s.on, s.cn, s.gm);
            }
            for (int n = 0; n < N; ++n)
                G[n][n] += supplyG[n] + gmin;
            if (inNode >= 0)
                G[inNode][inNode] += inG;

            // Gauss-Jordan inversion with partial pivoting
            double A[N][2 * N];
            for (int r = 0; r < N; ++r)
                for (int c = 0; c < 2 * N; ++c)
                    A[r][c] = c < N ? G[r][c] : (c - N == r ? 1.0 : 0.0);
            for (int col = 0; col < N; ++col)
            {
                int piv = col;
                for (int r = col + 1; r < N; ++r)
                    if (std::abs (A[r][col]) > std::abs (A[piv][col]))
                        piv = r;
                if (piv != col)
                    for (int c = 0; c < 2 * N; ++c)
                        std::swap (A[col][c], A[piv][c]);
                const double inv = 1.0 / A[col][col];
                for (int c = 0; c < 2 * N; ++c)
                    A[col][c] *= inv;
                for (int r = 0; r < N; ++r)
                {
                    if (r == col) continue;
                    const double f = A[r][col];
                    if (f == 0.0) continue;
                    for (int c = 0; c < 2 * N; ++c)
                        A[r][c] -= f * A[col][c];
                }
            }
            for (int r = 0; r < N; ++r)
                for (int c = 0; c < N; ++c)
                    Z[r][c] = A[r][c + N];
        }

        // Node-space helper vectors from Z
        struct NodeVectors
        {
            double zConst[N], zIn[N], zCap[NC > 0 ? NC : 1][N], zb[PP][N];
        };

        void nodeVectors (const double (&Z)[N][N], NodeVectors& nv) const
        {
            auto col = [&Z] (int n, int node) { return node < 0 ? 0.0 : Z[n][node]; };
            for (int n = 0; n < N; ++n)
            {
                double s = 0.0;
                for (int m = 0; m < N; ++m)
                    s += Z[n][m] * supplyI[m];
                nv.zConst[n] = s;
                nv.zIn[n] = inNode >= 0 ? Z[n][inNode] * inG : 0.0;
                for (int k = 0; k < NC; ++k)
                    nv.zCap[k][n] = col (n, caps[k].a) - col (n, caps[k].b);
                for (int p = 0; p < P; ++p)
                    nv.zb[p][n] = col (n, injTo[p]) - col (n, injFrom[p]);
            }
        }

        void buildStateSpace()
        {
            double Z[N][N];
            buildAndInvert (false, Z);
            NodeVectors nv;
            nodeVectors (Z, nv);

            auto diff = [] (const double* v, int a, int b) { return at (v, a) - at (v, b); };

            for (int m = 0; m < M; ++m)
            {
                cu[m] = diff (nv.zConst, ctrlP[m], ctrlM[m]);
                bu[m] = diff (nv.zIn, ctrlP[m], ctrlM[m]);
                for (int k = 0; k < NC; ++k)
                    Au[m][k] = diff (nv.zCap[k], ctrlP[m], ctrlM[m]);
                for (int p = 0; p < P; ++p)
                    K[m][p] = diff (nv.zb[p], ctrlP[m], ctrlM[m]);
            }
            for (int k = 0; k < NC; ++k)
            {
                const int a = caps[k].a, b = caps[k].b;
                cc[k] = diff (nv.zConst, a, b);
                bc[k] = diff (nv.zIn, a, b);
                for (int l = 0; l < NC; ++l)
                    Ac[k][l] = diff (nv.zCap[l], a, b);
                for (int p = 0; p < P; ++p)
                    Bc[k][p] = diff (nv.zb[p], a, b);
            }
            cy = at (nv.zConst, outNode);
            by = at (nv.zIn, outNode);
            for (int k = 0; k < NC; ++k)
                ay[k] = at (nv.zCap[k], outNode);
            for (int p = 0; p < P; ++p)
                dy[p] = at (nv.zb[p], outNode);
        }

        // Junction currents j(u) and Jacobian D = dj/du
        // (only the structurally non-zero entries of D are written; see newton())
        void evalDevices (const double* uu, double* j, double (&D)[PP][MP]) const
        {
            if constexpr (NB == 1)
            {
                const double vt = kThermalVoltage;
                const double ube = uu[0], ubc = uu[1];
                double dbe, dbc = 0.0;
                const double ebe = safeExp (ube / vt, dbe);
                // reverse-biased b-c junction (normal operation): e^(Vbc/Vt) < 1e-13, skip the exp
                const double ebc = ubc < -30.0 * vt ? 0.0 : safeExp (ubc / vt, dbc);
                const double q = 1.0 - ubc / bjt.Vaf;
                const double it = bjt.Is * (ebe - ebc);

                j[0] = it * q + gmin * (ube - ubc);
                D[0][0] = bjt.Is * dbe / vt * q + gmin;
                D[0][1] = -bjt.Is * dbc / vt * q - it / bjt.Vaf - gmin;

                if (effectiveBeta > 0.0)
                {
                    // transient: ideal base diode scaled to the DC base current (see prepare())
                    j[1] = bjt.Is / effectiveBeta * (ebe - 1.0) + gmin * ube;
                    D[1][0] = bjt.Is / effectiveBeta * dbe / vt + gmin;
                }
                else
                {
                    double dne;
                    const double ene = safeExp (ube / (bjt.Ne * vt), dne);
                    j[1] = bjt.Is / bjt.Bf * (ebe - 1.0) + bjt.Ise * (ene - 1.0) + gmin * ube;
                    D[1][0] = bjt.Is / bjt.Bf * dbe / vt + bjt.Ise * dne / (bjt.Ne * vt) + gmin;
                }

                j[2] = bjt.Is / bjt.Br * (ebc - 1.0) + gmin * ubc;
                D[2][1] = bjt.Is / bjt.Br * dbc / vt + gmin;
            }
            for (int d = 0; d < ND; ++d)
            {
                const auto& dp = dps[d];
                const double nvt = dp.m.N * kThermalVoltage;
                const double x = uu[dp.ctrl] / nvt;
                double dexp, dnexp, ep, en;
                if (std::abs (x) < 80.0) { ep = dexp = std::exp (x); en = dnexp = 1.0 / ep; }   // one exp for sinh/cosh
                else { ep = safeExp (x, dexp); en = safeExp (-x, dnexp); }
                j[dp.inj] = dp.m.Is * (ep - en) + gmin * uu[dp.ctrl];
                D[dp.inj][dp.ctrl] = dp.m.Is * (dexp + dnexp) / nvt + gmin;
            }
        }

        // Solve u = u0 + K j(u) in place (u holds the start value); returns the currents in j
        void newton (const double* u0, double* uu, int maxIter, double* j, double tol)
        {
            const double vt = kThermalVoltage;
            double D[PP][MP];
            lastIterations = 0;

            for (int iter = 0; iter < maxIter; ++iter)
            {
                lastIterations = iter + 1;
                evalDevices (uu, j, D);

                // J = I - K D, using the sparsity of D (each current depends only on
                // its own device's junction voltages)
                double J[MP][MP + 1];
                for (int m = 0; m < M; ++m)
                {
                    double f = uu[m] - u0[m];
                    for (int p = 0; p < P; ++p)
                        f -= K[m][p] * j[p];
                    for (int n = 0; n < M; ++n)
                        J[m][n] = (m == n) ? 1.0 : 0.0;
                    if constexpr (NB == 1)
                    {
                        J[m][0] -= K[m][0] * D[0][0] + K[m][1] * D[1][0];
                        J[m][1] -= K[m][0] * D[0][1] + K[m][2] * D[2][1];
                    }
                    for (int d = 0; d < ND; ++d)
                        J[m][dps[d].ctrl] -= K[m][dps[d].inj] * D[dps[d].inj][dps[d].ctrl];
                    J[m][M] = -f;
                }

                // Gaussian elimination with partial pivoting (M <= 4)
                for (int c = 0; c < M; ++c)
                {
                    int piv = c;
                    for (int r = c + 1; r < M; ++r)
                        if (std::abs (J[r][c]) > std::abs (J[piv][c])) piv = r;
                    if (piv != c)
                        for (int k = 0; k <= M; ++k) std::swap (J[c][k], J[piv][k]);
                    for (int r = c + 1; r < M; ++r)
                    {
                        const double f = J[r][c] / J[c][c];
                        for (int k = c; k <= M; ++k) J[r][k] -= f * J[c][k];
                    }
                }
                double du[MP];
                for (int r = M - 1; r >= 0; --r)
                {
                    double s = J[r][M];
                    for (int k = r + 1; k < M; ++k) s -= J[r][k] * du[k];
                    du[r] = s / J[r][r];
                }

                double unew[MP];
                for (int m = 0; m < M; ++m)
                    unew[m] = uu[m] + du[m];

                if constexpr (NB == 1)
                {
                    unew[0] = pnjlim (unew[0], uu[0], vt, vcritBjt);
                    unew[1] = pnjlim (unew[1], uu[1], vt, vcritBjt);
                }
                for (int d = 0; d < ND; ++d)
                {
                    const double nvt = dps[d].m.N * vt;
                    const int m = dps[d].ctrl;
                    const double s = unew[m] < 0.0 ? -1.0 : 1.0;
                    const double so = uu[m] * s;   // old value in the same orientation
                    unew[m] = s * pnjlim (std::abs (unew[m]), so, nvt, dps[d].vcrit);
                }

                double step = 0.0;
                for (int m = 0; m < M; ++m)
                    step = std::max (step, std::abs (unew[m] - uu[m]));

                // First-order update of the currents to the new point. Newton converges
                // quadratically, so after a step < tol the remaining error is ~tol^2/(2 n Vt);
                // with this correction the currents are consistent to the same order (and a
                // small-signal input is solved exactly to first order in one step).
                if constexpr (NB == 1)
                {
                    const double d0 = unew[0] - uu[0], d1 = unew[1] - uu[1];
                    j[0] += D[0][0] * d0 + D[0][1] * d1;
                    j[1] += D[1][0] * d0;
                    j[2] += D[2][1] * d1;
                }
                for (int d = 0; d < ND; ++d)
                {
                    const int m = dps[d].ctrl, p = dps[d].inj;
                    j[p] += D[p][m] * (unew[m] - uu[m]);
                }
                for (int m = 0; m < M; ++m)
                    uu[m] = unew[m];

                if (step < tol)
                    break;
            }
        }

        bool solveOperatingPoint (const double (&Z)[N][N])
        {
            NodeVectors nv;
            nodeVectors (Z, nv);

            // Initial guess: forward-active BJT, diodes off
            if (NB == 1) { u[0] = 0.65; u[1] = -4.0; }
            for (int d = 0; d < ND; ++d) u[dps[d].ctrl] = 0.0;

            // DC coupling matrix (overwritten by buildStateSpace() afterwards)
            for (int m = 0; m < M; ++m)
                for (int p = 0; p < P; ++p)
                    K[m][p] = at (nv.zb[p], ctrlP[m]) - at (nv.zb[p], ctrlM[m]);

            double j[PP] {};
            if constexpr (M > 0)
            {
                double u0[M];
                for (int m = 0; m < M; ++m)
                    u0[m] = at (nv.zConst, ctrlP[m]) - at (nv.zConst, ctrlM[m]);   // no input, caps open
                newton (u0, u, maxIterDc, j, toleranceDc);
            }

            for (int n = 0; n < N; ++n)
            {
                double v = nv.zConst[n];
                for (int p = 0; p < P; ++p)
                    v += nv.zb[p][n] * j[p];
                vDc[n] = v;
            }
            for (int m = 0; m < M; ++m)
                uDc[m] = u[m];
            for (int k = 0; k < NC; ++k)
                vcDc[k] = at (vDc, caps[k].a) - at (vDc, caps[k].b);
            yDc = at (vDc, outNode);

            bool ok = lastIterations < maxIterDc;
            for (int n = 0; n < N; ++n)
                ok = ok && std::isfinite (vDc[n]);
            return ok;
        }

        // topology
        int nRes = 0, nCap = 0, nVccs = 0, numDp = 0;
        Res res[MaxR] {};
        Cap caps[NC > 0 ? NC : 1] {};
        Vccs vccs[MaxS] {};
        double supplyG[N] {}, supplyI[N] {};
        int inNode = Gnd, outNode = 0;
        double inG = 0.0;

        bool hasBjt = false;
        int bjtB = Gnd, bjtC = Gnd, bjtE = Gnd;
        BjtModel bjt;
        DiodePair dps[ND > 0 ? ND : 1] {};
        double vcritBjt = 0.6;
        double effectiveBeta = 0.0;   // 0 = full Bf/Ise/Ne base current (DC solve)

        int ctrlP[MP] {}, ctrlM[MP] {}, injFrom[PP] {}, injTo[PP] {};

        // state-space matrices
        double fs = 48000.0;
        bool dirty = false;
        double gc[NC > 0 ? NC : 1] {};
        double Au[MP][NC > 0 ? NC : 1] {}, bu[MP] {}, cu[MP] {}, K[MP][PP] {};
        double Ac[NC > 0 ? NC : 1][NC > 0 ? NC : 1] {}, Bc[NC > 0 ? NC : 1][PP] {}, bc[NC > 0 ? NC : 1] {}, cc[NC > 0 ? NC : 1] {};
        double ay[NC > 0 ? NC : 1] {}, dy[PP] {}, by = 0.0, cy = 0.0;

        // DC solution and state
        double vDc[N] {}, vcDc[NC > 0 ? NC : 1] {}, uDc[MP] {}, yDc = 0.0;
        double h[NC > 0 ? NC : 1] {}, u[MP] {}, uPrev[MP] {}, y = 0.0;
        int lastIterations = 0;
    };
} // namespace bmd
