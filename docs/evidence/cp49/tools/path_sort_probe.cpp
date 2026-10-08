#include <filesystem>
#include <vector>
#include <algorithm>
#include <string>
#include <ctime>
#include <iostream>
namespace fs=std::filesystem;
int main(int argc,char**argv){if(argc!=2)return 2;std::vector<fs::path> paths;for(auto&e:fs::recursive_directory_iterator(argv[1]))if(e.is_regular_file())paths.push_back(e.path());std::reverse(paths.begin(),paths.end());double original=0,cached=0;std::size_t conversions=0;for(int repeat=0;repeat<10;++repeat){auto a=paths;auto t=std::clock();std::sort(a.begin(),a.end(),[&](const auto&x,const auto&y){conversions+=2;return x.generic_string()<y.generic_string();});original+=1000.0*(std::clock()-t)/CLOCKS_PER_SEC;t=std::clock();std::vector<std::pair<std::string,fs::path>> keys;keys.reserve(paths.size());for(auto&p:paths)keys.emplace_back(p.generic_string(),p);std::sort(keys.begin(),keys.end(),[](const auto&x,const auto&y){return x.first<y.first;});cached+=1000.0*(std::clock()-t)/CLOCKS_PER_SEC;for(std::size_t i=0;i<a.size();++i)if(a[i]!=keys[i].second)return 1;}std::cout<<paths.size()<<" "<<original/10<<" "<<cached/10<<" "<<conversions/10<<" "<<paths.size()<<"\n";}
