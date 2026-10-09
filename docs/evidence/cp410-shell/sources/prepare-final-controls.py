from pathlib import Path
import json,subprocess
p=Path('.build/cp410-shell').resolve();s=(p/'write-control.cpp').read_text().replace('#include <stdexcept>','#include <stdexcept>\n#include <vector>')
s=s.replace('std::filesystem::create_directories("files");for(size_t i=0;i<n;i++){auto path="files/out-"+std::to_string(i);','std::filesystem::create_directories("files");std::ifstream input("targets.txt");std::vector<std::string> names;std::string line;while(std::getline(input,line))names.push_back(line);if(names.size()!=n)return 3;for(size_t i=0;i<n;i++){auto path=names[i];')
(p/'write-control-exact.cpp').write_text(s);subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(p/'write-control-exact.cpp'),'-o',str(p/'write-control-exact')],check=True)
