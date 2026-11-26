#include "flyeye.h"
#include <algorithm>
#include <cmath>

static uint64_t splitmix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

void FlyEye::init(const Connectome& C, const EyeParams& p) {
    P = p;
    npos_ = P.cols * P.rows;
    photo_idx_ = C.photos;
    lam_idx_ = C.lamina;
    lc_cache_ = C.lc_loom;
    photo_at_.assign(npos_, {});
    lam_at_.assign(npos_, {});
    // Deterministic spread of real cells over the ommatidia grid.
    for (size_t k = 0; k < photo_idx_.size(); ++k)
        photo_at_[(uint32_t)(splitmix(k + 1) % (uint64_t)npos_)].push_back(photo_idx_[k]);
    for (size_t k = 0; k < lam_idx_.size(); ++k)
        lam_at_[(uint32_t)(splitmix(k + 1000003) % (uint64_t)npos_)].push_back(lam_idx_[k]);
    lum_.assign(npos_, 0.5f);
    lum_new_.assign(npos_, 0.5f);
    adapt_.assign(npos_, 0.5f);
    feat_c_.assign(npos_, 0.0f);
    feat_d_.assign(npos_, 0.0f);
    feat_m_.assign(npos_, 0.0f);
    drv_photo_.assign(npos_, 0.0f);
    drv_lam_.assign(npos_, 0.0f);
    frame_ = 0;
}

void FlyEye::process_frame(const uint8_t* gray, int W, int H) {
    const int C_ = P.cols, R = P.rows;
    // sample luminance per ommatidium (single pixel at the position center)
    for (int r = 0; r < R; ++r) {
        for (int c = 0; c < C_; ++c) {
            int px = (int)((c + 0.5) * W / C_);
            int py = (int)((r + 0.5) * H / R);
            px = px > W - 1 ? W - 1 : px;
            py = py > H - 1 ? H - 1 : py;
            float v = (float)(gray[py * W + px]) / 255.0f;
            lum_new_[r * C_ + c] = v;
        }
    }
    // adaptation (frame-based EMA; 30 fps assumed)
    const float a_adapt = 1.0f / (float)(P.adapt_tau * 30.0);
    double sum_m = 0, sum_d = 0, sum_c = 0;
    for (int r = 0; r < R; ++r) {
        for (int c = 0; c < C_; ++c) {
            int p = r * C_ + c;
            float L = lum_new_[p], Lp = lum_[p];
            // neighbors (clamped at grid edges)
            int cl = c > 0 ? p - 1 : p, cr = c < C_ - 1 ? p + 1 : p;
            int cu = r > 0 ? p - C_ : p, cd = r < R - 1 ? p + C_ : p;
            // Hassenstein-Reichardt half-correlators (1-frame delay)
            float mx = L * lum_[cr] - Lp * lum_new_[cr] - (L * lum_[cl] - Lp * lum_new_[cl]);
            float my = L * lum_[cd] - Lp * lum_new_[cd] - (L * lum_[cu] - Lp * lum_new_[cu]);
            float m = std::fabs(mx) + std::fabs(my);
            adapt_[p] += (L - adapt_[p]) * a_adapt;
            feat_c_[p] = L - adapt_[p];          // signed contrast (photo: bright, lamina: dark)
            feat_d_[p] = std::fabs(L - Lp);
            feat_m_[p] = m;
            sum_m += m;
            sum_d += feat_d_[p];
            sum_c += std::fabs(feat_c_[p]);
        }
    }
    lum_.swap(lum_new_);
    motion_mean_ = sum_m / npos_;
    transient_mean_ = sum_d / npos_;
    contrast_mean_ = sum_c / npos_;
    ++frame_;
}

void FlyEye::drive_pure(Brain& B) {
    // Real sign chain: photoreceptors depolarize to LIGHT and release
    // histamine which INHIBITS lamina monopolar cells; lamina is therefore
    // active in darkness (disinhibition). Photos encode brightening events,
    // lamina external drive encodes darkening events, so a dark looming
    // object coherently activates lamina through BOTH channels.
    for (int p = 0; p < npos_; ++p) {
        float c = feat_c_[p];
        float bright = c > 0.0f ? c : 0.0f;
        float dark = c < 0.0f ? -c : 0.0f;
        float rate = (float)P.photo_base
                   + (float)P.photo_contrast * bright
                   + (float)P.photo_transient * feat_d_[p]
                   + (float)P.photo_motion * feat_m_[p];
        if (rate < 0) rate = 0;
        if (rate > 1) rate = 1;
        for (uint32_t i : photo_at_[p]) { B.clamp_on[i] = 1; B.clamp_val[i] = rate; }
        float lam = (float)P.lam_contrast * dark
                  + (float)P.lam_motion * feat_m_[p]
                  + (float)P.lam_transient * feat_d_[p];
        if (lam < 0) lam = 0;
        if (lam > 1.2f) lam = 1.2f;
        for (uint32_t i : lam_at_[p]) B.ext[i] = lam;
        drv_photo_[p] = rate;   // live overlay copies only, not used by the model
        drv_lam_[p] = lam;
    }
}

void FlyEye::drive_lc(Brain& B, double gain) {
    float v = (float)(gain * (motion_mean_ + transient_mean_));
    if (v < 0) v = 0;
    if (v > 1.5f) v = 1.5f;
    for (uint32_t i : photo_idx_) B.clamp_on[i] = 0;
    for (uint32_t i : lam_idx_) B.ext[i] = 0;
    for (uint32_t i : lc_cache_) B.ext[i] = v;
}

void FlyEye::release(Brain& B) {
    for (uint32_t i : photo_idx_) { B.clamp_on[i] = 0; B.clamp_val[i] = 0; }
    for (uint32_t i : lam_idx_) B.ext[i] = 0;
    for (uint32_t i : lc_cache_) B.ext[i] = 0;
}
