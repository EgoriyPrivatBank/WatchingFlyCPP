#pragma once
// Video input pipeline: any video file (or a YouTube URL, downloaded with
// yt-dlp) is decoded by an ffmpeg subprocess into a stream of 320x180
// grayscale frames piped straight into the fly eye. No temp frame files.
#include <cstdint>
#include <string>

class VideoSource {
public:
    static constexpr int FW = 320, FH = 180;

    // path: local video file. max_sec > 0 limits decoded duration.
    bool open(const std::string& path, double max_sec, std::string* err);
    ~VideoSource();
    // Returns 1 and fills buf (FW*FH bytes), or 0 at end of stream.
    int read_frame(uint8_t* buf);
    static constexpr double FPS = 30.0;

private:
    FILE* fp_ = nullptr;  // pipe from ffmpeg
};

// Downloads url with yt-dlp into outdir, returns the local file path
// (empty string + *err on failure). max_sec limits the downloaded section.
std::string youtube_download(const std::string& url, const std::string& outdir,
                             double max_sec, std::string* err);

// Makes out/browser.mp4 (H.264 yuv420p + faststart, AAC if the source has
// audio) so any browser can play /video. Returns src unchanged on failure.
// The fly eye keeps decoding the ORIGINAL file — this copy is display-only.
std::string prepare_browser_copy(const std::string& src);

// ---- live download progress (BUG 5) ----
struct DlStatus {
    std::string stage;      // "download" | "convert" | "sim" | "" (idle)
    double percent = 0;     // 0..100
    double speed_mbs = 0;   // MiB/s
    double eta_s = 0;       // seconds
    std::string error;      // non-empty = failed
};
DlStatus dl_status();
void dl_cancel();           // request cancel (checked between stages)
