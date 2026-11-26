#pragma once
// Simulation core: two whole-connectome dynamics models.
//
//  MODEL_RATE — leaky rate model. Input is a sign-weighted average of
//  deviations of presynaptic rates from a reference level ref_rate
//  (I = sum sign*w*(r_j - ref_rate) / sum w, zero at baseline), passed
//  through a gain/transfer with a bias; states decay with tau. The reference
//  subtraction keeps the network at a low resting point instead of
//  saturating, since most synapses are excitatory.
//
//  MODEL_LIF — leaky integrate-and-fire after Shiu, P.K. et al. 2024, Nature
//  ("A Drosophila computational brain model reveals whole-brain spontaneous
//  activity and stimulus-induced switching"): dv/dt = (v0 - v + g)/tau_m,
//  dg/dt = -g/tau_syn, v0=-52 mV, vth=-45 mV, tau_m=20 ms, tau_syn=5 ms,
//  t_ref=2.2 ms, per-synapse event amplitude 0.275 mV (no weight
//  normalization), Poisson background noise. Their 1.8 ms synaptic delay is
//  sub-timestep here (dt = 5 ms) and is not modeled.
//
//  Neurotransmitter signs (both models): ACh/Glu +1, GABA/Hist -1 (histamine
//  gates chloride channels, e.g. photoreceptor -> lamina), DA/Oct/5-HT +0.3
//  (slow modulators), unknown +0.5.
#include "connectome.h"
#include <vector>

enum Model { MODEL_RATE = 0, MODEL_LIF = 1 };

// Thread count for all Brain instances (set from --threads; 16 by default).
// Results are deterministic and independent of this value at a fixed seed:
// threads partition disjoint neuron ranges and never share writes.
extern int g_threads;

// Noise seed for all Brain instances (set from --seed; 42 by default).
// Different seeds give different noise realizations, same statistics.
extern uint64_t g_seed;

struct SimParams {
    double dt = 0.005;               // 5 ms timestep
    uint64_t seed = 42;
    // rate model
    double tau = 0.05;               // state decay
    double gain = 1.2;               // loop gain*S must stay < 1 for a stable rest state
    double bias = 0.02;              // spontaneous baseline drive
    double ref_rate = 0.05;          // deviations from this rate drive postsynapses
    double norm_pow = 1.0;           // divide input by (sum w)^norm_pow
    double noise_sigma = 0.01;       // gaussian sigma on normalized input
    // LIF (Shiu et al. 2024, adapted)
    double v_rest = -52.0;           // mV
    double v_th = -45.0;             // mV
    double v_reset = -52.0;          // mV
    double tau_m = 0.020;            // s
    double tau_syn = 0.005;          // s
    double t_ref = 0.0022;           // s
    double w_mV_per_syn = 0.275;     // mV per synapse per spike
    double lif_scale = 1.0;          // global LIF weight scale
    double noise_hz = 30.0;          // background Poisson rate (reduced from Shiu's 150 Hz:
    double noise_mV = 1.0;           // 150 Hz x 2 mV kept the whole CNS near threshold)
    double rate_tau = 0.05;          // EMA window for LIF rate readout
    double photo_max_hz = 100.0;     // normalized rate 1.0 -> Hz
    double ext_mV = 7.0;             // LIF: ext drive 1.0 -> mV into g per step
};

class Brain {
public:
    const Connectome& C;
    SimParams P;
    int model = MODEL_RATE;
    int threads = 16;
    double t = 0.0;
    uint64_t steps = 0;

    // state
    std::vector<float>    rate;        // [0,1] per neuron
    std::vector<float>    v, g, refract;  // LIF (mV, mV, s)
    std::vector<uint8_t>  spike;
    std::vector<uint8_t>  clamp_on;    // externally driven neurons (photoreceptors)
    std::vector<float>    clamp_val;   // normalized rate [0,1]
    std::vector<float>    ext;         // additive drive (rate units / LIF via ext_mV)

    // precomputed
    std::vector<float>    nt_eff;      // sign*scale per neuron
    std::vector<float>    ew;          // per edge: signed weight (rate units or mV)
    std::vector<float>    inv_norm;    // per neuron: 1/(sum w)^norm_pow (rate model)

    Brain(const Connectome& c) : C(c) {}
    void init(int model_);
    void step();
    double mean_rate() const;

    // Live-mode "lesion" experiment: silence (on) or restore (off) all outgoing
    // edges of the given neurons. Default state is off and the simulation is
    // then bit-identical to a non-live run.
    void set_lesion(const std::vector<uint32_t>& idx, bool on);

    // clamp helpers (photoreceptor drive)
    void set_clamp_all(const std::vector<uint32_t>& idx, float val);
    void set_clamp_hash(const std::vector<uint32_t>& idx, double lo, double hi, uint64_t seed);
    void clear_clamps();
    void clear_ext();

    static float hash01(uint64_t x) {
        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL; x ^= x >> 33;
        return (float)(x & 0xFFFFFFu) / 16777216.0f;
    }

private:
    std::vector<float> rate_next_;
    std::vector<uint8_t> lesion_mask_;
    void step_rate();
    void step_lif();
    template <class F>
    void run_parallel(F&& f, size_t n);
};
