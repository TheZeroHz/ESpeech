/**
 * TinyCFG Host Benchmark — MSc evaluation harness
 *
 * Compiles TinyCFG for PC (no ESP32) and measures:
 *   - Parse latency (µs): mean, min, max, std, p95
 *   - Accuracy: pass/fail vs labelled dataset
 *   - Parser metrics: tokens, confidence, RAM estimate
 *
 * Build:  make          (Linux/macOS/MinGW)
 *         build.bat     (Windows)
 * Run:    ./tinycfg_bench [dataset.csv] [iterations]
 * Analyze: python analyze_results.py results_raw.csv
 */

#include <Arduino.h>
#include <TinyCFG.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct BenchCase {
    int id;
    char category[32];
    char input[192];
    int expect_pass;
    char expect_action[32];
    char description[96];
};

struct BenchResult {
    BenchCase tc;
    int actual_pass;
    int action_count;
    char first_action[32];
    double mean_us;
    double min_us;
    double max_us;
    double std_us;
    double p95_us;
    uint8_t tokens;
    float confidence;
    uint16_t ram_bytes;
    uint8_t phonetic_hits;
    uint8_t glued_segments;
    uint8_t noise_skipped;
};

static const char* DEFAULT_DATASET = "dataset/dataset.csv";
static const char* DEFAULT_OUTPUT  = "results/results_raw.csv";
static const int   DEFAULT_ITERS   = 500;
static const int   WARMUP_ITERS    = 50;

static void trim(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
    char* p = s;
    while (*p == ' ') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static bool parse_csv_line(char* line, char** fields, int max_fields) {
    int count = 0;
    char* p = line;
    while (*p && count < max_fields) {
        fields[count++] = p;
        while (*p && *p != ',') p++;
        if (*p == ',') {
            *p++ = '\0';
            trim(fields[count - 1]);
        }
    }
    if (count > 0) trim(fields[count - 1]);
    return count >= 5;
}

static bool load_dataset(const char* path, std::vector<BenchCase>& out) {
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open dataset: %s\n", path);
        return false;
    }

    char line[512];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return false;
    }

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '#') continue;
        char* fields[8];
        if (!parse_csv_line(line, fields, 8)) continue;

        BenchCase bc;
        memset(&bc, 0, sizeof(bc));
        bc.id = atoi(fields[0]);
        strncpy(bc.category, fields[1], sizeof(bc.category) - 1);
        strncpy(bc.input, fields[2], sizeof(bc.input) - 1);
        bc.expect_pass = atoi(fields[3]);
        if (fields[4] && fields[4][0]) {
            strncpy(bc.expect_action, fields[4], sizeof(bc.expect_action) - 1);
        }
        if (fields[5]) {
            strncpy(bc.description, fields[5], sizeof(bc.description) - 1);
        }
        out.push_back(bc);
    }

    fclose(f);
    return !out.empty();
}

static bool tree_has_action(const TcfgTaskNode* root, const char* expect) {
    if (!root || !expect || !expect[0]) return true;
    for (uint8_t i = 0; i < root->childCount; ++i) {
        const TcfgTaskNode* c = root->children[i];
        if (c->type == TCFG_NODE_ACTION) {
            const char* name = TinyCFG::actionName(c->actionId);
            if (strstr(name, expect)) return true;
        }
    }
    return false;
}

static int count_actions(const TcfgTaskNode* root) {
    if (!root) return 0;
    int n = 0;
    for (uint8_t i = 0; i < root->childCount; ++i) {
        if (root->children[i]->type == TCFG_NODE_ACTION) n++;
    }
    return n;
}

static void first_action_name(const TcfgTaskNode* root, char* out, size_t outLen) {
    out[0] = '\0';
    if (!root) return;
    for (uint8_t i = 0; i < root->childCount; ++i) {
        if (root->children[i]->type == TCFG_NODE_ACTION) {
            strncpy(out, TinyCFG::actionName(root->children[i]->actionId), outLen - 1);
            return;
        }
    }
}

static void compute_stats(std::vector<double>& samples, double& mean, double& minv,
                          double& maxv, double& stdv, double& p95) {
    if (samples.empty()) {
        mean = minv = maxv = stdv = p95 = 0.0;
        return;
    }
    std::sort(samples.begin(), samples.end());
    minv = samples.front();
    maxv = samples.back();
    double sum = 0.0;
    for (double v : samples) sum += v;
    mean = sum / (double)samples.size();
    double var = 0.0;
    for (double v : samples) {
        double d = v - mean;
        var += d * d;
    }
    stdv = sqrt(var / (double)samples.size());
    size_t p95idx = (size_t)((samples.size() - 1) * 0.95);
    p95 = samples[p95idx];
}

static BenchResult run_case(TinyCFG& parser, const BenchCase& tc, int iters) {
    BenchResult br;
    memset(&br, 0, sizeof(br));
    br.tc = tc;

    for (int i = 0; i < WARMUP_ITERS; ++i) {
        parser.parse(tc.input);
    }

    std::vector<double> samples;
    samples.reserve((size_t)iters);

    using clock = std::chrono::steady_clock;
    bool last_ok = false;
    for (int i = 0; i < iters; ++i) {
        auto t0 = clock::now();
        last_ok = parser.parse(tc.input);
        auto t1 = clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        samples.push_back(us);

        if (i == iters - 1) {
            br.actual_pass = last_ok ? 1 : 0;
            if (last_ok && tc.expect_action[0] && !tree_has_action(parser.getTaskTree(), tc.expect_action)) {
                br.actual_pass = 0;
            }
            const TcfgParseStats& st = parser.getStats();
            br.tokens = st.tokensTotal;
            br.confidence = st.confidence;
            br.ram_bytes = st.ramUsedBytes;
            br.phonetic_hits = st.phoneticHits;
            br.glued_segments = st.gluedSegments;
            br.noise_skipped = st.noiseSkipped;
            br.action_count = count_actions(parser.getTaskTree());
            first_action_name(parser.getTaskTree(), br.first_action, sizeof(br.first_action));
        }
    }

    compute_stats(samples, br.mean_us, br.min_us, br.max_us, br.std_us, br.p95_us);
    return br;
}

static void write_results(const char* path, const std::vector<BenchResult>& results,
                          int iters, const char* platform) {
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "ERROR: cannot write %s\n", path);
        return;
    }

    fprintf(f, "# TinyCFG benchmark raw results\n");
    fprintf(f, "# platform,%s\n", platform);
    fprintf(f, "# iterations_per_case,%d\n", iters);
    fprintf(f, "# grammar,%s v%u patterns=%u\n",
        TinyCFG::getGrammarName(), TinyCFG::getGrammarVersion(), TinyCFG::getPatternCount());
    fprintf(f, "# sizeof_TinyCFG,%u\n", (unsigned)sizeof(TinyCFG));
    fprintf(f, "id,category,input,expect_pass,actual_pass,action_count,first_action,"
               "mean_us,min_us,max_us,std_us,p95_us,tokens,confidence,ram_bytes,"
               "phonetic_hits,glued_segments,noise_skipped,description\n");

    for (const BenchResult& br : results) {
        fprintf(f, "%d,%s,\"%s\",%d,%d,%d,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%.4f,%u,%u,%u,%u,\"%s\"\n",
            br.tc.id, br.tc.category, br.tc.input,
            br.tc.expect_pass, br.actual_pass, br.action_count,
            br.first_action[0] ? br.first_action : "-",
            br.mean_us, br.min_us, br.max_us, br.std_us, br.p95_us,
            br.tokens, br.confidence, br.ram_bytes,
            br.phonetic_hits, br.glued_segments, br.noise_skipped,
            br.tc.description);
    }
    fclose(f);
}

static void print_summary_table(const std::vector<BenchResult>& results) {
    printf("\n");
    printf("================================================================================\n");
    printf("  TinyCFG BENCHMARK SUMMARY (host)\n");
    printf("================================================================================\n");
    printf("%-16s %6s %6s %10s %10s %10s\n",
        "Category", "Cases", "Acc%%", "Mean(us)", "P95(us)", "Conf");
    printf("--------------------------------------------------------------------------------\n");

    std::vector<std::string> cats;
    for (const BenchResult& br : results) {
        bool found = false;
        for (const std::string& c : cats) {
            if (c == br.tc.category) { found = true; break; }
        }
        if (!found) cats.push_back(br.tc.category);
    }

    int total = 0, correct = 0;
    double total_mean = 0.0;

    for (const std::string& cat : cats) {
        int n = 0, ok = 0;
        double sum_mean = 0.0, sum_p95 = 0.0, sum_conf = 0.0;
        for (const BenchResult& br : results) {
            if (cat != br.tc.category) continue;
            n++;
            if (br.actual_pass == br.tc.expect_pass) ok++;
            sum_mean += br.mean_us;
            sum_p95 += br.p95_us;
            sum_conf += br.confidence;
        }
        if (n == 0) continue;
        total += n;
        correct += ok;
        total_mean += sum_mean;
        printf("%-16s %6d %5.1f%% %10.1f %10.1f %9.2f\n",
            cat.c_str(), n, 100.0 * ok / n, sum_mean / n, sum_p95 / n, sum_conf / n);
    }

    printf("--------------------------------------------------------------------------------\n");
    printf("%-16s %6d %5.1f%% %10.1f\n",
        "OVERALL", total, 100.0 * correct / total, total_mean / total);
    printf("================================================================================\n\n");
}

int main(int argc, char** argv) {
    const char* dataset_path = DEFAULT_DATASET;
    const char* output_path  = DEFAULT_OUTPUT;
    int iters = DEFAULT_ITERS;

    if (argc >= 2) dataset_path = argv[1];
    if (argc >= 3) iters = atoi(argv[2]);
    if (argc >= 4) output_path = argv[3];
    if (iters < 10) iters = 10;

    std::vector<BenchCase> cases;
    if (!load_dataset(dataset_path, cases)) {
        return 1;
    }

    printf("TinyCFG Host Benchmark\n");
    printf("  Grammar:     %s v%u (%u patterns)\n",
        TinyCFG::getGrammarName(), TinyCFG::getGrammarVersion(), TinyCFG::getPatternCount());
    printf("  Dataset:     %s (%u cases)\n", dataset_path, (unsigned)cases.size());
    printf("  Iterations:  %d per case (+ %d warmup)\n", iters, WARMUP_ITERS);
    printf("  sizeof:      %u bytes\n", (unsigned)sizeof(TinyCFG));
    printf("  Output:      %s\n\n", output_path);

    TinyCFG parser;
    parser.setFuzzyMatch(true);
    parser.setPhoneticRecovery(true);
    parser.setErrorRecovery(true);

    std::vector<BenchResult> results;
    results.reserve(cases.size());

    for (size_t i = 0; i < cases.size(); ++i) {
        printf("  [%3u/%3u] %-14s  %s\r",
            (unsigned)(i + 1), (unsigned)cases.size(), cases[i].category, cases[i].input);
        fflush(stdout);
        results.push_back(run_case(parser, cases[i], iters));
    }
    printf("\n");

    write_results(output_path, results, iters, "host-pc");
    print_summary_table(results);

    printf("Raw results written to: %s\n", output_path);
    printf("Run analysis: python analyze_results.py %s\n", output_path);
    return 0;
}
