#pragma once
// Minimal fast CSV parser for the fly connectome files.
// Handles quoted fields with "" escapes, CRLF line endings and UTF-8 BOM.
// Fields are returned as (pointer, length) spans into the caller's buffer.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace csv {

struct Field {
    const char* p = nullptr;
    uint32_t    n = 0;
    bool empty() const { return n == 0; }
    bool is(const char* t) const {
        size_t L = strlen(t);
        return n == L && memcmp(p, t, L) == 0;
    }
    bool starts_with(const char* t) const {
        size_t L = strlen(t);
        return n >= L && memcmp(p, t, L) == 0;
    }
    uint64_t to_u64() const {  // digits only, no sign handling needed here
        uint64_t v = 0;
        for (uint32_t i = 0; i < n; ++i) {
            char c = p[i];
            if (c >= '0' && c <= '9') v = v * 10 + uint64_t(c - '0');
        }
        return v;
    }
    double to_double() const {
        char tmp[64];
        uint32_t L = n < 63 ? n : 63;
        memcpy(tmp, p, L);
        tmp[L] = 0;
        return strtod(tmp, nullptr);
    }
};

// Splits one CSV line in place (unescapes quoted fields by shifting left).
// Advances *pp past the line's '\n'. Returns the number of fields (<= cap).
inline size_t split_line(char** pp, Field* out, size_t cap) {
    char* s = *pp;
    size_t n = 0;
    for (;;) {
        char* start;
        if (*s == '"') {
            ++s;
            start = s;
            char* w = s;  // write pointer, always <= s
            while (*s && *s != '\n') {
                if (*s == '"') {
                    if (s[1] == '"') { *w++ = '"'; s += 2; }
                    else { ++s; break; }  // closing quote
                } else *w++ = *s++;
            }
            out[n].p = start;
            out[n].n = uint32_t(w - start);
        } else {
            start = s;
            while (*s && *s != ',' && *s != '\n' && *s != '\r') ++s;
            out[n].p = start;
            out[n].n = uint32_t(s - start);
        }
        if (++n == cap) break;
        if (*s == ',') { ++s; continue; }
        break;
    }
    while (*s && *s != '\n') ++s;  // skip overlong remainder of the line
    if (*s == '\n') ++s;
    *pp = s;
    return n;
}

// Reads the whole file into memory; strips a UTF-8 BOM if present.
inline bool read_file(const char* path, std::vector<char>& buf) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return false; }
    buf.resize((size_t)sz + 1);
    size_t rd = fread(buf.data(), 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    char* p = buf.data();
    if (rd >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF) {
        memmove(p, p + 3, rd - 2);  // content plus trailing NUL
    }
    return true;
}

}  // namespace csv
