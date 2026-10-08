#include "Json.h"
#include "RuntimeJson.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <valgrind/callgrind.h>
int main(int argc,char** argv) {
 if(argc!=4)return 2;
 std::ifstream file(argv[2]);std::string input((std::istreambuf_iterator<char>(file)),{});
 json::Document document;std::string error;
 const bool convert=std::string(argv[1])=="convert";
 if(convert&&!nift_json::parse(input,document,error))return 3;
 const int rounds=std::stoi(argv[3]);std::size_t sum=0;
 CALLGRIND_TOGGLE_COLLECT;
 for(int i=0;i<rounds;++i){
  if(convert){auto value=nift::runtime_from_json(document);sum+=value.is_array()?value.array.size():value.object.size();}
  else{json::Document parsed;if(!nift_json::parse(input,parsed,error)){std::cerr<<error;return 4;}sum+=parsed.is_array()?parsed.array.size():parsed.object.size();}
 }
 CALLGRIND_TOGGLE_COLLECT;
 std::cout<<sum<<'\n';
}
