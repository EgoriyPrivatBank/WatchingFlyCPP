#include "metrics.h"
#include <algorithm>

double g_escape_refractory = 1.5;

std::vector<Metrics::Event> detect_escapes(const std::vector<double>& t,
                                           const std::vector<double>& gf,
                                           const std::vector<double>& lc4,
                                           double refract_s) {
    // Mirrors Metrics::update exactly (defaults of MetricsParams).
    const double abs_min = 0.025, k = 3.0, tau = 10.0, warmup = 2.0;
    std::vector<Metrics::Event> events;
    if (t.size() < 2 || gf.size() != t.size()) return events;
    double dt = t[1] - t[0];
    if (dt <= 0) dt = 1.0 / 30.0;
    const double a = dt / (dt + tau);
    double m1 = gf[0], m2 = gf[0] * gf[0];
    double last = -1e9;
    for (size_t i = 0; i < t.size(); ++i) {
        double x = gf[i];
        m1 += a * (x - m1);                 // EMA baseline
        m2 += a * (x * x - m2);
        double v = m2 - m1 * m1;
        double sd = std::sqrt(v > 1e-12 ? v : 1e-12);
        double thr = std::max(abs_min, m1 + k * sd);
        if (t[i] >= warmup && x > thr && t[i] - last >= refract_s) {
            last = t[i];
            float lc = i < lc4.size() ? (float)lc4[i] : 0.0f;
            events.push_back({t[i], (float)x, lc});
        }
    }
    return events;
}

double Metrics::gmean_of(const Brain& B, const std::vector<uint32_t>& g) {
    if (g.empty()) return 0.0;
    double s = 0.0;
    for (uint32_t i : g) s += B.rate[i];
    return s / (double)g.size();
}

void Metrics::init(const Connectome& C) {
    C_ = &C;
    P = MetricsParams();
    P.escape_refract = g_escape_refractory;  // --escape-refractory
    dn_leg_.clear();
    dn_tct_.clear();
    for (uint32_t d : C.dnv) {
        if (C.dn_dom[d] == DN_DOM_LEG) dn_leg_.push_back(d);
        else if (C.dn_dom[d] == DN_DOM_FLIGHT) dn_tct_.push_back(d);
    }
    events.clear();
    escapes_total = 0;
    last_escape_t_ = -1e9;
    gf_base_ = Ema{}; sat_da_ = Ema{}; sat_mbon_ = Ema{}; sat_oct_ = Ema{}; sat_ser_ = Ema{};
    beh_leg_ = Ema{}; beh_tct_ = Ema{}; beh_dn_ = Ema{};
}

void Metrics::update(const Brain& B, double t, double dt,
                     double motion, double transient, double contrast) {
    t_ = t;
    motion_ = motion;
    transient_ = transient;
    contrast_ = contrast;

    gf = gmean_of(B, C_->gf);
    lc4 = gmean_of(B, C_->lc_loom);
    m_photo = gmean_of(B, C_->photos);
    m_lam = gmean_of(B, C_->lamina);
    m_t4t5 = gmean_of(B, C_->t4t5);
    m_hsvs = gmean_of(B, C_->hs_vs);
    m_mbon = gmean_of(B, C_->mbons);
    m_da = gmean_of(B, C_->dan);
    m_oct = gmean_of(B, C_->oan);
    m_ser = gmean_of(B, C_->ser);
    m_dnleg = gmean_of(B, dn_leg_);
    m_dntct = gmean_of(B, dn_tct_);
    m_dnall = gmean_of(B, C_->dnv);
    m_motor = gmean_of(B, C_->motor);
    m_all = B.mean_rate();

    const double a_base = dt / (dt + P.beh_tau);
    const double a_gf = dt / (dt + P.gf_tau);
    const double a_sat = dt / (dt + P.sat_tau);
    const bool warmed = t >= P.warmup;

    // ---- escape attempts (giant fiber DNp01) ----
    gf_base_.update(gf, a_gf);
    escape_drive = gf_base_.z(gf, 1e-4);
    double thr = std::max(P.escape_abs_min, gf_base_.mean() + P.escape_k_sigma * gf_base_.sd());
    if (warmed && gf > thr && t - last_escape_t_ >= P.escape_refract) {
        ++escapes_total;
        last_escape_t_ = t;
        events.push_back({t, (float)gf, (float)lc4});
    }
    bool in_escape = (t - last_escape_t_) <= P.escape_window;

    // ---- satisfaction (DA + MBON - OCT - SER, adaptive z-scores) ----
    sat_da_.update(m_da, a_sat);
    sat_mbon_.update(m_mbon, a_sat);
    sat_oct_.update(m_oct, a_sat);
    sat_ser_.update(m_ser, a_sat);
    if (warmed) {
        double z = P.w_da * sat_da_.z(m_da, P.sat_sd_floor)
                 + P.w_mbon * sat_mbon_.z(m_mbon, P.sat_sd_floor)
                 - P.w_oct * sat_oct_.z(m_oct, P.sat_sd_floor)
                 - P.w_ser * sat_ser_.z(m_ser, P.sat_sd_floor);
        satisfaction = 0.5 + 0.5 * std::tanh(P.sat_gain * z);
    } else {
        satisfaction = 0.5;
    }

    // ---- behavior classification ----
    beh_leg_.update(m_dnleg, a_base);
    beh_tct_.update(m_dntct, a_base);
    beh_dn_.update(m_dnall, a_base);
    if (!warmed) behavior = BEH_REST;
    else if (in_escape) behavior = BEH_ESCAPE;
    else if (m_dntct > beh_tct_.thr(4.0, P.beh_rel)) behavior = BEH_FLIGHT;
    else if (m_dnleg > beh_leg_.thr(4.0, P.beh_rel)) behavior = BEH_WALK;
    else if (m_dnall > beh_dn_.thr(4.0, P.beh_rel)) behavior = BEH_ACTIVE;
    else behavior = BEH_REST;
}

void Metrics::write_csv_header(FILE* f) {
    fprintf(f, "t,gf,lc4,escape_drive,escapes_cum,satisfaction,behavior,"
               "motion,transient,contrast,photo,lamina,t4t5,hsvs,mbon,da,oct,ser,"
               "dnleg,dntct,dnall,motor,all,lesion\n");
}

void Metrics::write_csv_row(FILE* f) {
    // 24 columns, must match write_csv_header exactly
    fprintf(f, "%.3f,%.5f,%.5f,%.3f,%lld,%.4f,%d,%.4f,%.4f,%.4f,"
               "%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%s\n",
            t_, gf, lc4, escape_drive, (long long)escapes_total, satisfaction, behavior,
            motion_, transient_, contrast_,
            m_photo, m_lam, m_t4t5, m_hsvs, m_mbon, m_da, m_oct, m_ser,
            m_dnleg, m_dntct, m_dnall, m_motor, m_all, lesion_name.c_str());
}
