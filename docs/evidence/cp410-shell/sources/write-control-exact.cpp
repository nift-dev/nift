#include <filesystem>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <vector>
int main(int argc,char** argv){if(argc!=3)return 2;auto n=std::stoul(argv[2]);std::string mode=argv[1],payload;for(int i=0;i<7;i++)payload+="0123456789abcdef";payload+="0123456789abcde\n";std::filesystem::create_directories("files");std::ifstream input("targets.txt");std::vector<std::string> names;std::string line;while(std::getline(input,line))names.push_back(line);if(names.size()!=n)return 3;for(size_t i=0;i<n;i++){auto path=names[i];auto temp=mode=="direct"?path:path+".tmp";int fd=open(temp.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0666);if(fd<0||write(fd,payload.data(),payload.size())!=128)throw std::runtime_error("write failed");if(mode=="fsync"&&fsync(fd))throw std::runtime_error("fsync failed");if(close(fd))throw std::runtime_error("close failed");if(mode!="direct")std::filesystem::rename(temp,path);}std::cout<<"OK\n";}
