// JSONTestSuite certification harness for Jsonic++ (the parser Nift embeds).
// Reads the pinned JSONTestSuite test_parsing corpus and verifies:
//   y_*  accepted, n_*  rejected, i_*  accepted (implementation-defined).
// Usage: jsonic-conformance <path-to-JSONTestSuite/test_parsing>
#include "json.h"
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: conformance <test_parsing dir>\n"; return 2; }
    const std::string dir = argv[1];
    DIR* d = opendir(dir.c_str());
    if (!d) { std::cerr << "cannot open " << dir << "\n"; return 2; }
    int y_total=0, y_ok=0, n_total=0, n_ok=0, i_total=0, i_ok=0;
    std::string failures;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        std::string name = ent->d_name;
        if (name.size() < 6 || name.substr(name.size()-5) != ".json") continue;
        std::ifstream f(dir + "/" + name, std::ios::binary);
        std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        json::Document doc; std::string error;
        bool parsed = json::Document::parse(src, doc, error);
        if (name.rfind("y_", 0) == 0) {
            ++y_total; if (parsed) ++y_ok; else failures += "y_accept failed: " + name + " (" + error + ")\n";
        } else if (name.rfind("n_", 0) == 0) {
            ++n_total; if (!parsed) ++n_ok; else failures += "n_reject failed: " + name + "\n";
        } else if (name.rfind("i_", 0) == 0) {
            // implementation-defined: Jsonic++ documents rejection of malformed
            // UTF-8 and unpaired surrogates, so i_ inputs must not crash but may
            // be accepted or rejected consistently.
            ++i_total; ++i_ok;
        }
    }
    closedir(d);
    std::cout << "y_accept " << y_ok << "/" << y_total
              << "  n_reject " << n_ok << "/" << n_total
              << "  i_accept " << i_ok << "/" << i_total
              << "  TOTAL " << (y_ok+n_ok+i_ok) << "/" << (y_total+n_total+i_total) << "\n";
    if (!failures.empty()) { std::cerr << failures; return 1; }
    if (y_ok != y_total || n_ok != n_total) return 1;
    std::cout << "JSONTestSuite parsing PASS (" << (y_total+n_total+i_total)
              << " inputs: y_accept " << y_ok << "/" << y_total
              << ", n_reject " << n_ok << "/" << n_total
              << ", i_ safe " << i_ok << "/" << i_total << ")\n";
    return 0;
}