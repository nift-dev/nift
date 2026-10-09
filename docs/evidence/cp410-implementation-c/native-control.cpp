#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <cctype>
namespace fs=std::filesystem;
int main(int argc,char**argv){if(argc!=2)return 2;std::string op=argv[1];std::vector<std::string> paths;std::ifstream manifest("targets.txt");std::string p;while(std::getline(manifest,p))paths.push_back(p);std::uintmax_t total=0;std::ofstream output;if(op=="concat")output.open("output.tmp",std::ios::binary|std::ios::trunc);std::string result;
for(const auto&path:paths){if(op=="metadata"){auto status=fs::status(path);if(fs::is_regular_file(status))total+=fs::file_size(path);}else if(op=="move")fs::rename(path,fs::path("moved")/fs::path(path).filename());else if(op=="copy")fs::copy_file(path,fs::path("copied")/fs::path(path).filename(),fs::copy_options::overwrite_existing);else if(op=="concat"){std::ifstream input(path,std::ios::binary);if(!input)return 3;output<<input.rdbuf();}else if(op=="string"){result="alpha";for(char&c:result)c=std::toupper(static_cast<unsigned char>(c));std::size_t pos=0;while((pos=result.find("A",pos))!=std::string::npos){result.replace(pos,1,"a");++pos;}}else return 2;}
if(op=="concat"){output.close();if(!output)return 3;fs::rename("output.tmp","output");}if(op=="metadata")std::cout<<total<<'\n';else if(op=="string")std::cout<<result<<'\n';else std::cout<<"OK\n";}
