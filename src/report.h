#pragma once
// Stage 8: self-contained offline HTML report with canvas charts, built from
// the metrics time-series CSV and the escape event list.
#include "metrics.h"
#include <string>

void write_html_report(const std::string& csv_path, const std::string& html_path,
                       const Metrics& M, double duration_s, int64_t nframes,
                       const char* wiring_label);

// Rebuilds out/report.html from an existing metrics CSV without re-simulating
// (escape events are recovered where escapes_cum increments).
void regenerate_report(const char* csv_path, const char* wiring_label);
