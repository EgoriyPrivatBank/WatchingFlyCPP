#include "video.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static bool file_exists(const char* p) {
    FILE* f = fopen(p, "rb");
    if (f) { fclose(f); return true; }
    return false;
}
// 8.3 short path lets ffmpeg/cmd.exe handle non-ASCII (e.g. Cyrillic) paths.
static std::string short_path(const std::string& p) {
    wchar_t w[1024], out[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, w, 1024) <= 0) return p;
    DWORD n = GetShortPathNameW(w, out, 1024);
    if (n == 0 || n >= 1024) return p;
    char buf[2048];
    if (WideCharToMultiByte(CP_UTF8, 0, out, -1, buf, 2048, nullptr, nullptr) <= 0) return p;
    return std::string(buf);
}
// Tool lookup: FLYSIM_* env override, then ..\tools next to the project,
// then a tools\ folder inside it, then PATH.
static std::string tool_path(const char* env, const char* local1,
                             const char* local2, const char* fallback) {
    if (const char* e = getenv(env))
        if (*e && file_exists(e)) return e;
    if (file_exists(local1)) return local1;
    if (file_exists(local2)) return local2;
    return fallback;
}
static std::string ffmpeg_path() {
    return tool_path("FLYSIM_FFMPEG",
                     "..\\tools\\ffmpeg-master-latest-win64-gpl\\bin\\ffmpeg.exe",
                     "tools\\ffmpeg-master-latest-win64-gpl\\bin\\ffmpeg.exe",
                     "ffmpeg");
}
static std::string ytdlp_path() {
    return tool_path("FLYSIM_YTDLP", "..\\tools\\yt-dlp.exe",
                     "tools\\yt-dlp.exe", "yt-dlp");
}

// cmd.exe strips the first/last quote of the command line, so a command whose
// arguments contain quotes must itself be wrapped in an extra quote pair.
static std::string wrap_for_cmd(const std::string& cmd) { return "\"" + cmd + "\""; }

bool VideoSource::open(const std::string& path, double max_sec, std::string* err) {
    if (fp_) { _pclose(fp_); fp_ = nullptr; }
    char tail[64] = "";
    if (max_sec > 0) snprintf(tail, sizeof tail, " -t %.0f", max_sec);
    char cmd[4096];
    snprintf(cmd, sizeof cmd,
             "\"%s\" -loglevel error%s -i \"%s\" -an -vf scale=%d:%d -r 30 "
             "-f rawvideo -pix_fmt gray -",
             ffmpeg_path().c_str(), tail, short_path(path).c_str(), FW, FH);
    fp_ = _popen(wrap_for_cmd(cmd).c_str(), "rb");
    if (!fp_) { *err = "failed to start ffmpeg"; return false; }
    return true;
}

VideoSource::~VideoSource() {
    if (fp_) _pclose(fp_);
}

int VideoSource::read_frame(uint8_t* buf) {
    if (!fp_) return 0;
    size_t need = (size_t)FW * FH;
    return fread(buf, 1, need, fp_) == need ? 1 : 0;
}

// ---------------- live download progress (BUG 5) ----------------
static std::mutex g_dl_mtx;
static DlStatus g_dl;
DlStatus dl_status() { std::lock_guard<std::mutex> lk(g_dl_mtx); return g_dl; }
void dl_cancel() { std::lock_guard<std::mutex> lk(g_dl_mtx); g_dl.error = "cancelled by user"; }
static void dl_set(const DlStatus& s) { std::lock_guard<std::mutex> lk(g_dl_mtx); g_dl = s; }
static bool dl_cancelled() { std::lock_guard<std::mutex> lk(g_dl_mtx); return !g_dl.error.empty(); }

// Parses one "--newline" progress line of yt-dlp: [download]  12.3% of ... at 1.23MiB/s ETA 00:12
static bool parse_ytdlp_line(const char* line, DlStatus& s) {
    if (strncmp(line, "[download]", 10) != 0) return false;
    const char* p = strstr(line, "% of");
    if (!p) return false;
    const char* q = p;
    while (q > line && (*q == ' ' || isdigit((unsigned char)*q) || *q == '.')) --q;
    s.percent = atof(q + 1);
    if ((p = strstr(line, " at ")) && (p = strstr(p, "iB/s"))) {
        double v = atof(p - 0);
        const char* r = p;
        while (r > line && (*r == '.' || isdigit((unsigned char)*r))) --r;
        v = atof(r + 1);
        s.speed_mbs = strstr(line, "GiB/s") ? v * 1024.0 : (strstr(line, "MiB/s") ? v : v / 1024.0);
    }
    if ((p = strstr(line, "ETA "))) {
        int mm = 0, ss = 0;
        if (sscanf(p + 4, "%d:%d", &mm, &ss) == 2) s.eta_s = mm * 60.0 + ss;
    }
    return true;
}

std::string prepare_browser_copy(const std::string& src) {
    std::error_code ec;
    std::filesystem::create_directories("out", ec);
    const std::string dst = "out/browser.mp4";
    if (src == dst) return src;  // never transcode a file onto itself
    std::remove(dst.c_str());
    char cmd[4096];
    snprintf(cmd, sizeof cmd,
             "\"%s\" -y -loglevel error -i \"%s\" -c:v libx264 -preset veryfast -crf 23 "
             "-pix_fmt yuv420p -movflags +faststart -c:a aac -b:a 96k \"%s\"",
             ffmpeg_path().c_str(), short_path(src).c_str(), dst.c_str());
    if (system(wrap_for_cmd(cmd).c_str()) == 0 && file_exists(dst.c_str())) {
        printf("[video] browser copy (H.264): %s\n", dst.c_str());
        dl_set({"sim", 100, 0, 0, ""});
        return dst;
    }
    printf("[video] browser copy failed, serving the original\n");
    dl_set({"sim", 100, 0, 0, ""});
    return src;
}

std::string youtube_download(const std::string& url, const std::string& outdir,
                             double max_sec, std::string* err) {
    std::error_code ec;
    std::filesystem::create_directories(outdir, ec);
    for (auto& e : std::filesystem::directory_iterator(outdir, ec))
        if (e.path().filename().string().rfind("ytvideo", 0) == 0)
            std::filesystem::remove(e.path(), ec);

    dl_set({"download", 0, 0, 0, ""});
    char cmd[4096];
    // 480p is plenty for the 320x180 fly eye; cascade to 720p/any if absent.
    // -N 8 parallelizes m3u8 fragment downloads (YouTube throttles ~200-600 KiB/s).
    // --newline: one progress line per update, parseable.
    const char* fmt = "\"%s\" --newline --no-playlist --ffmpeg-location \"%s\" -N 8 %s"
                      "-f \"bv*[height<=480]/bv*[height<=720]/bv*\" "
                      "-o \"%s/ytvideo.%%(ext)s\" \"%s\" 2>&1";
    char secs[64] = "";
    if (max_sec > 0) snprintf(secs, sizeof secs, "--download-sections \"*0-%.0f\" ", max_sec);
    // ffmpeg dir for section cutting
    std::string ffdir = ffmpeg_path();
    size_t slash = ffdir.find_last_of("\\/");
    if (slash != std::string::npos) ffdir = ffdir.substr(0, slash);
    snprintf(cmd, sizeof cmd, fmt, ytdlp_path().c_str(), ffdir.c_str(), secs,
             outdir.c_str(), url.c_str());
    // stream yt-dlp output and parse progress live (BUG 5)
    FILE* pf = _popen(wrap_for_cmd(cmd).c_str(), "r");
    if (!pf) { dl_set({"", 0, 0, 0, "cannot start yt-dlp"}); *err = "cannot start yt-dlp"; return ""; }
    char line[512];
    DlStatus s;
    s.stage = "download";
    std::string tail;   // last non-progress lines for error extraction
    while (fgets(line, sizeof line, pf)) {
        if (dl_cancelled()) { _pclose(pf); *err = "cancelled by user"; return ""; }
        line[strcspn(line, "\r\n")] = 0;
        if (parse_ytdlp_line(line, s)) { dl_set(s); continue; }
        if (line[0] && strncmp(line, "[download]", 10) != 0 && strlen(line) > 12) {
            tail = line;    // keep the last meaningful line for errors
            if (strstr(line, "ERROR") || strstr(line, "error"))
                tail = line;
        }
    }
    int rc = _pclose(pf);
    if (dl_cancelled()) { *err = "cancelled by user"; return ""; }
    if (rc != 0) {
        DlStatus f; f.stage = ""; f.error = tail.empty() ? "yt-dlp failed (bad link / no network / restricted video)" : tail;
        dl_set(f);
        *err = f.error;
        return "";
    }
    std::string path;
    for (const char* ext : { ".mp4", ".webm", ".mkv" }) {
        std::string p = outdir + "/ytvideo" + ext;
        if (file_exists(p.c_str())) { path = p; break; }
    }
    if (path.empty()) {
        dl_set({"", 0, 0, 0, "downloaded file not found"});
        *err = "downloaded file not found in " + outdir;
        return "";
    }
    dl_set({"convert", 50, 0, 0, ""});   // browser-copy stage reports below
    return path;
}
