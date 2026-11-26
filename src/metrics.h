#pragma once
// Behavioral metrics read out from the simulated connectome (stage 5).
//  - Escape attempts: bursts of the giant fiber DNp01 (root ids 10001/10010,
//    verified in stage 2) above an adaptive threshold (EMA baseline + k*sd,
//    absolute floor), refractory period 1.5 s.
//  - Satisfaction (amendment 7, proxy): adaptive z-scores of real
//    neuromodulatory populations combined as  DA + MBON - OCT - SER,
//    squashed to [0,1] with tanh. Not a measured emotion.
//  - Behavior: escape window > flight (tectulum DNs) > walk/groom (leg DNs)
//    > active (all DNs) > rest, all by adaptive thresholds.
#include "connectome.h"
#include "sim.h"
#include <cmath>
#include <cstdio>
#include <vector>

enum Behavior { BEH_REST = 0, BEH_ACTIVE, BEH_WALK, BEH_FLIGHT, BEH_ESCAPE, BEH_COUNT };
inline const char* beh_name(int b) {
    static const char* N[BEH_COUNT] = { "rest", "active", "walk/groom", "flight", "jump/escape" };
    return N[b];
}

// Escape refractory period in seconds (default 1.5: one visual event must not
// yield a double-counted burst; the old 0.7 s let single stimuli register twice).
extern double g_escape_refractory;

struct MetricsParams {
    double escape_abs_min = 0.025;  // absolute GF floor for an escape event
    double escape_k_sigma = 3.0;    // GF threshold = max(floor, base + k*sd)
    double escape_refract = 1.5;    // s (default overridden by g_escape_refractory)
    double escape_window = 0.5;     // s marked as jump/escape behavior
    double gf_tau = 10.0;           // GF baseline (habituation) window, s
    double beh_tau = 20.0;          // behavior baseline window, s
    double sat_tau = 10.0;          // satisfaction normalization window, s
    double sat_sd_floor = 0.0015;   // min sd for satisfaction z-scores
    double sat_gain = 1.5;
    double w_da = 1.0, w_mbon = 0.6, w_oct = 0.6, w_ser = 0.3;
    double warmup = 2.0;            // s: no events/satisfaction before warm-up ends
    double beh_rel = 0.35;          // behavior threshold: base + max(4sd, rel*base)
};

class Metrics {
public:
    void init(const Connectome& C);
    // t: seconds; dt: frame period (s); eye features for logging
    void update(const Brain& B, double t, double dt,
                double motion, double transient, double contrast);
    void write_csv_header(FILE* f);
    void write_csv_row(FILE* f);

    struct Event { double t; float gf, lc4; };
    std::vector<Event> events;
    std::string lesion_name = "none";  // live-mode lesion marker (CSV column)

    double gf = 0, lc4 = 0, escape_drive = 0, satisfaction = 0.5;
    int64_t escapes_total = 0;
    int behavior = BEH_REST;
    double m_photo, m_lam, m_t4t5, m_hsvs, m_mbon, m_da, m_oct, m_ser,
           m_dnleg, m_dntct, m_dnall, m_motor, m_all;

private:
    struct Ema {
        double m1 = 0, m2 = 1e-6;
        bool init = false;
        void update(double x, double a) {
            if (!init) { m1 = x; m2 = x * x; init = true; return; }
            m1 += a * (x - m1);
            m2 += a * (x * x - m2);
        }
        double mean() const { return m1; }
        double sd() const {
            double v = m2 - m1 * m1;
            return std::sqrt(v > 1e-12 ? v : 1e-12);
        }
        double thr(double k, double rel) const {
            double d = k * sd();
            double r = rel * std::fabs(m1);
            return m1 + (d > r ? d : r);
        }
        double z(double x, double sd_floor) const {
            double s = sd();
            return (x - m1) / (s > sd_floor ? s : sd_floor);
        }
    };
    MetricsParams P;
    const Connectome* C_ = nullptr;
    std::vector<uint32_t> dn_leg_, dn_tct_;
    Ema gf_base_, sat_da_, sat_mbon_, sat_oct_, sat_ser_;
    Ema beh_leg_, beh_tct_, beh_dn_;
    double last_escape_t_ = -1e9;
    double motion_ = 0, transient_ = 0, contrast_ = 0, t_ = 0;
    static double gmean_of(const Brain& B, const std::vector<uint32_t>& g);
};

// Re-runs the exact escape detector (warmup 2 s; GF threshold
// max(0.025, EMA baseline + 3 sigma), tau = 10 s) over a gf time series with
// the given refractory period. Single source of events for `flysim report`.
std::vector<Metrics::Event> detect_escapes(const std::vector<double>& t,
                                           const std::vector<double>& gf,
                                           const std::vector<double>& lc4,
                                           double refract_s);
