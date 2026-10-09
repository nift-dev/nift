#include "/home/nick/Repositories/nift/jsonic/jsonic/include/json.h"
#include "Phases.h"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    std::ifstream stream(argv[1]); std::string text((std::istreambuf_iterator<char>(stream)),{});
    json::ParseOptions options;
    options.duplicate_keys=std::string(argv[2])=="reject"?json::DuplicateKeyPolicy::Reject:json::DuplicateKeyPolicy::Preserve;
    json::Document result; json::ParseDiagnostic diagnostic;
    bool ok=false;
    {cp51::Phase phase(cp51::Factory);ok=json::Document::parse(text,result,diagnostic,options);}
    std::cout << (ok?"PASS":"ERROR") << '\t' << (ok?result.object.size():diagnostic.offset) << '\t' << diagnostic.message << '\n';
}
