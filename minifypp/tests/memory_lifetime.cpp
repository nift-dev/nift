#include <minify/Minify.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <unistd.h>

static void require(bool ok, const std::string& msg) { if (!ok) { std::cerr << msg << '\n'; std::exit(1); } }
static long rss_kib() {
    std::ifstream f("/proc/self/statm"); long total=0,res=0; if (!(f>>total>>res)) return -1;
    return res * sysconf(_SC_PAGESIZE) / 1024;
}
struct Case { minify::Format f; std::string valid; std::string invalid; };
int main(int argc,char**argv){
    int iterations=argc>1?std::atoi(argv[1]):100;
    const std::string blob(256*1024,'x');
    std::vector<Case> cases={
      {minify::Format::Html,"<main>  hello <!--x--> <pre> a  b </pre><div data-x=\""+blob+"\">x</div></main>","<div data-x=\"unterminated>"},
      {minify::Format::Css,"/*x*/ .a { color: red; content: \"a  b\"; --blob: \""+blob+"\"; }","a{/* unterminated"},
      {minify::Format::JavaScript,"const x=1; /*x*/ const s=\""+blob+"\"; const r=/a\\/b/g;\nreturn\n x;","const r=/unterminated\nx();"},
      {minify::Format::Jsx,"const x=<div title=\"a  b\"><span>{name}</span></div>; const s=\""+blob+"\";","const x=<div>{a+1</div>;"},
      {minify::Format::Json,"{\"items\":[1,true,null,{\"blob\":\""+blob+"\"}]}","{\"a\":"},
      {minify::Format::Xml,"<root><item a=\"b\">text</item><blob>"+blob+"</blob></root>","<root><!--"},
      {minify::Format::Svg,"<svg viewBox=\"0 0 10 10\"><path d=\"M0 0 L10 10\"/><text>"+blob+"</text></svg>","<svg data-x=\"unterminated>"}
    };
    std::string out,err;
    long warm=-1,mid=-1,end=-1;
    for(int it=0;it<iterations;++it){
      for(const auto& c:cases){
        require(minify::run(c.f,c.valid,out,err),"valid input failed: "+err);
        std::string second;
        require(minify::run(c.f,out,second,err),"second pass failed: "+err);
        require(!c.invalid.empty() && !minify::run(c.f,c.invalid,out,err),"invalid input unexpectedly accepted");
      }
      // alternate very small inputs after large buffers to stress retained capacity/reuse
      require(minify::json("{\"x\":1}",out,err),err);
      require(minify::css("a { b: c; }",out,err),err);
      if(it==iterations/10) warm=rss_kib();
      if(it==iterations/2) mid=rss_kib();
    }
    end=rss_kib();
    std::cout << "iterations="<<iterations<<" warm_rss_kib="<<warm<<" midpoint_rss_kib="<<mid<<" final_rss_kib="<<end<<"\n";
    return 0;
}
