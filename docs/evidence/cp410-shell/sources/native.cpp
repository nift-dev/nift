#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>
namespace fs=std::filesystem;
void walk(const fs::path& p,std::vector<fs::path>& out){std::error_code ec;for(fs::directory_iterator i(p,fs::directory_options::skip_permission_denied,ec),end;!ec&&i!=end;i.increment(ec)){const auto&e=*i;auto name=e.path().filename().string();if(!name.empty()&&name[0]=='.')continue;if(name.size()>=4&&name.substr(name.size()-4)==".dat"&&fs::exists(e.path(),ec))out.push_back(fs::absolute(e.path()).lexically_normal());if(e.is_directory(ec)&&!e.is_symlink(ec))walk(e.path(),out);}}
int main(int argc,char**){std::vector<fs::path> out;walk("files",out);std::vector<std::pair<std::string,fs::path>> keyed;keyed.reserve(out.size());for(auto& x:out)keyed.emplace_back(x.generic_string(),std::move(x));std::sort(keyed.begin(),keyed.end(),[](const auto&a,const auto&b){return a.first<b.first;});for(size_t i=0;i<out.size();++i)out[i]=std::move(keyed[i].second);out.erase(std::unique(out.begin(),out.end()),out.end());const auto base=fs::current_path();std::vector<std::string> shown;for(const auto&m:out){std::error_code ec;auto r=fs::relative(m,base,ec);shown.push_back((ec?m:r).generic_string());}if(argc>1){for(auto&s:shown)std::cout<<s<<'\n';}else std::cout<<shown.size()<<'\n';}
