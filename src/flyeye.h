#pragma once
// The fly eye: maps video frames onto the connectome's photoreceptors and
// lamina (pure-input mode, amendment 3). Luminance is sampled on a grid of
// "ommatidia" spread over the frame (the frontal screen; both eyes share the
// field in v1). Per position we compute:
//   - adapted contrast  c = L - L_adapt   (photoreceptor adaptation, tau ~ 2 s)
//   - transient        d = |L(t) - L(t-1)|
//   - EMD motion       m (Hassenstein-Reichardt half-correlator, 1-frame delay)
// Photoreceptors are CLAMPED to base + gc*|c| + gt*d + gm*m; lamina cells get
// an additive external drive lm*m + lt*d. Nothing else is driven: looming
// selectivity must emerge through the real wiring up to LC4/LPLC2 and the
// giant fiber.
//
// Retinotopy caveat: the connectome export has no morphology, so the
// photoreceptor<->position assignment is a deterministic hash (types and
// counts are real, exact spatial layout is not).
#include "connectome.h"
#include "sim.h"
#include <cstdint>
#include <vector>

struct EyeParams {
    int cols = 32, rows = 24;        // ommatidia grid (~768, ~ per-eye count in a real fly)
    double adapt_tau = 3.5;          // photoreceptor adaptation, seconds
    double photo_base = 0.08;
    double photo_contrast = 0.55;    // weight of |L - L_adapt|
    double photo_transient = 0.5;    // weight of frame-to-frame |dL|
    double photo_motion = 0.35;      // weight of EMD motion energy
    double lam_contrast = 0.9;       // lamina ext drive from darkening ("яркость", OFF channel)
    double lam_motion = 0.9;         // lamina ext drive from motion
    double lam_transient = 0.7;      // lamina ext drive from transients
};

class FlyEye {
public:
    void init(const Connectome& C, const EyeParams& p);
    // Computes per-position features from a grayscale frame (W*H bytes).
    void process_frame(const uint8_t* gray, int W, int H);
    // Pure mode: photoreceptor clamps + lamina external drive.
    void drive_pure(Brain& B);
    // Debug mode (amendment 3): direct drive of LC4/LPLC2 from pooled motion+transient.
    void drive_lc(Brain& B, double gain);
    void release(Brain& B);          // clear clamps/ext of driven cells

    double motion_mean() const { return motion_mean_; }
    double transient_mean() const { return transient_mean_; }
    double contrast_mean() const { return contrast_mean_; }
    int positions() const { return npos_; }
    const std::vector<uint32_t>& photos() const { return photo_idx_; }
    const std::vector<uint32_t>& lamina() const { return lam_idx_; }
    // per-position drive values (live overlay): photoreceptor clamp rate,
    // lamina external drive, EMD motion feature
    const float* drv_photo() const { return drv_photo_.data(); }
    const float* drv_lam() const { return drv_lam_.data(); }
    const float* feat_motion() const { return feat_m_.data(); }

private:
    EyeParams P;
    int npos_ = 0;
    std::vector<uint32_t> photo_idx_, lam_idx_, lc_cache_;
    std::vector<std::vector<uint32_t>> photo_at_, lam_at_;  // position -> cells
    std::vector<float> lum_, lum_new_, adapt_;
    std::vector<float> feat_c_, feat_d_, feat_m_;           // per position
    std::vector<float> drv_photo_, drv_lam_;                // live overlay copies
    double motion_mean_ = 0, transient_mean_ = 0, contrast_mean_ = 0;
    int frame_ = 0;
};
