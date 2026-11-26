#include "sim.h"
#include <cmath>
#include <cstdio>
#include <thread>

int g_threads = 16;
uint64_t g_seed = 42;

static float nt_eff_of(uint8_t nt) {
    switch (nt) {
        case NT_ACH: case NT_GLUT: return 1.0f;
        case NT_GABA: case NT_HIST: return -1.0f;
        case NT_DA: case NT_OCT: case NT_SER: return 0.3f;
        default: return 0.5f;
    }
}

template <class F>
void Brain::run_parallel(F&& f, size_t n) {
    if (threads <= 1 || n < 4096) {
        f((size_t)0, n);
        return;
    }
    int T = threads;
    size_t chunk = (n + T - 1) / T;
    std::vector<std::thread> th;
    th.reserve(T);
    for (int a = 0; a < T; ++a) {
        size_t lo = (size_t)a * chunk;
        size_t hi = lo + chunk < n ? lo + chunk : n;
        if (lo >= hi) break;
        th.emplace_back(f, lo, hi);
    }
    for (auto& x : th) x.join();
}

void Brain::init(int model_) {
    model = model_;
    P.seed = g_seed;
    const size_t N = C.n(), M = C.m();
    nt_eff.resize(N);
    for (size_t i = 0; i < N; ++i) nt_eff[i] = nt_eff_of(C.neurons[i].nt);
    ew.resize(M);
    std::vector<float> wsum(N, 0.0f);
    const float wlif = (float)(P.w_mV_per_syn * P.lif_scale);
    for (size_t k = 0; k < M; ++k) {
        const Edge& e = C.edges[k];
        float w = (model == MODEL_LIF) ? wlif * e.w : e.w;
        ew[k] = nt_eff[e.pre] * w;
        wsum[e.post] += e.w;
    }
    inv_norm.resize(N);
    for (size_t i = 0; i < N; ++i)
        inv_norm[i] = wsum[i] > 0.0f ? (float)(1.0 / std::pow((double)wsum[i], P.norm_pow)) : 0.0f;
    rate.assign(N, 0.0f);
    rate_next_.assign(N, 0.0f);
    v.assign(N, (float)P.v_rest);
    g.assign(N, 0.0f);
    refract.assign(N, 0.0f);
    spike.assign(N, 0);
    clamp_on.assign(N, 0);
    clamp_val.assign(N, 0.0f);
    ext.assign(N, 0.0f);
    t = 0.0;
    steps = 0;
}

void Brain::step() {
    if (model == MODEL_RATE) step_rate();
    else step_lif();
}

// ---------------- rate model ----------------
void Brain::step_rate() {
    const size_t N = C.n();
    const float lam = (float)std::exp(-P.dt / P.tau);
    const float gain = (float)P.gain, bias = (float)P.bias;
    const float refr = (float)P.ref_rate;
    const float sig = (float)P.noise_sigma;
    const uint64_t skey = steps * 0x9E3779B97F4A7C15ULL + P.seed * 0x2545F4914F6CDD1DULL;
    const auto& in_off = C.in_off;
    const auto& edges = C.edges;

    auto worker = [&](size_t a, size_t b) {
        for (size_t i = a; i < b; ++i) {
            float I = 0.0f;
            const uint32_t end = in_off[i + 1];
            for (uint32_t k = in_off[i]; k < end; ++k)
                I += ew[k] * (rate[edges[k].pre] - refr);
            I *= inv_norm[i];
            float u = hash01((uint64_t)i * 0xD1B54A32D192ED03ULL + skey);
            I += (u * 2.0f - 1.0f) * sig + ext[i];
            float target = gain * I + bias;
            if (target < 0.0f) target = 0.0f;
            if (target > 1.0f) target = 1.0f;
            rate_next_[i] = lam * rate[i] + (1.0f - lam) * target;
        }
    };
    run_parallel(worker, N);
    rate.swap(rate_next_);
    for (size_t i = 0; i < N; ++i)
        if (clamp_on[i]) rate[i] = clamp_val[i];
    ++steps;
    t += P.dt;
}

// ---------------- LIF (Shiu et al. 2024) ----------------
void Brain::step_lif() {
    const size_t N = C.n();
    const float dtf = (float)P.dt;
    const float decay_g = (float)std::exp(-P.dt / P.tau_syn);
    const float dv = dtf / (float)P.tau_m;
    const float v_rest = (float)P.v_rest, v_th = (float)P.v_th;
    const float v_reset = (float)P.v_reset, t_ref = (float)P.t_ref;
    const float p_noise = (float)(P.noise_hz * P.dt);
    const float noise_mV = (float)P.noise_mV;
    const float p_clamp1 = dtf * (float)P.photo_max_hz;   // spike prob at rate 1.0
    const float r_spike = 1.0f / (dtf * (float)P.photo_max_hz);
    const float ext_mV = (float)P.ext_mV;
    const uint64_t skey = steps * 0x9E3779B97F4A7C15ULL + P.seed * 0x2545F4914F6CDD1DULL;
    const auto& in_off = C.in_off;
    const auto& edges = C.edges;

    // phase 1: membrane update, spikes (clamped photoreceptors spike stochastically
    // at their clamped rate; their v/g are unused)
    auto ph1 = [&](size_t a, size_t b) {
        for (size_t i = a; i < b; ++i) {
            if (clamp_on[i]) {
                float u = hash01((uint64_t)i * 0x2545F4914F6CDD1DULL + skey);
                spike[i] = (u < clamp_val[i] * p_clamp1) ? 1 : 0;
                continue;
            }
            if (refract[i] > 0.0f) {
                refract[i] -= dtf;
                spike[i] = 0;
                continue;
            }
            v[i] += dv * (v_rest - v[i] + g[i]);
            float u = hash01((uint64_t)i * 0xD1B54A32D192ED03ULL + skey);
            if (u < p_noise) v[i] += noise_mV;
            if (v[i] > v_th) {
                spike[i] = 1;
                v[i] = v_reset;
                g[i] = 0.0f;
                refract[i] = t_ref;
            } else {
                spike[i] = 0;
            }
        }
    };
    run_parallel(ph1, N);

    // phase 2: accumulate synaptic input on each post from this step's spikes,
    // update the rate readout (EMA of the spike train)
    const float alpha = dtf / (dtf + (float)P.rate_tau);
    auto ph2 = [&](size_t a, size_t b) {
        for (size_t p = a; p < b; ++p) {
            float dg = 0.0f;
            const uint32_t end = in_off[p + 1];
            for (uint32_t k = in_off[p]; k < end; ++k)
                if (spike[edges[k].pre]) dg += ew[k];
            g[p] = g[p] * decay_g + dg;
            if (ext[p] != 0.0f) g[p] += ext[p] * ext_mV;
            float r = rate[p] + alpha * ((spike[p] ? r_spike : 0.0f) - rate[p]);
            rate[p] = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r);
            if (clamp_on[p]) rate[p] = clamp_val[p];
        }
    };
    run_parallel(ph2, N);
    ++steps;
    t += P.dt;
}

void Brain::set_lesion(const std::vector<uint32_t>& idx, bool on) {
    if (lesion_mask_.size() != C.n()) lesion_mask_.assign(C.n(), 0);
    for (uint32_t i : idx) lesion_mask_[i] = on ? 1 : 0;
    const float wlif = (float)(P.w_mV_per_syn * P.lif_scale);
    for (size_t k = 0; k < C.m(); ++k) {
        const Edge& e = C.edges[k];
        if (!lesion_mask_[e.pre]) continue;
        ew[k] = on ? 0.0f : nt_eff[e.pre] * (model == MODEL_LIF ? wlif * e.w : e.w);
    }
}

double Brain::mean_rate() const {
    double s = 0.0;
    for (float r : rate) s += r;
    return rate.empty() ? 0.0 : s / (double)rate.size();
}

void Brain::set_clamp_all(const std::vector<uint32_t>& idx, float val) {
    for (uint32_t i : idx) { clamp_on[i] = 1; clamp_val[i] = val; }
}
void Brain::set_clamp_hash(const std::vector<uint32_t>& idx, double lo, double hi, uint64_t seed) {
    for (uint32_t i : idx) {
        clamp_on[i] = 1;
        clamp_val[i] = (float)(lo + (hi - lo) * hash01((uint64_t)i * 0x9E3779B97F4A7C15ULL + seed));
    }
}
void Brain::clear_clamps() {
    for (size_t i = 0; i < clamp_on.size(); ++i) { clamp_on[i] = 0; clamp_val[i] = 0.0f; }
}
void Brain::clear_ext() {
    for (size_t i = 0; i < ext.size(); ++i) ext[i] = 0.0f;
}
