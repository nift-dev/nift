#include <ParserHelpers.h>
#include <sstream>
#include <cassert>
#include <iostream>
#include <limits>
#include <algorithm>
class Input final:public std::streambuf {
    std::string bytes;
    std::size_t position=0,chunk,fail;
public:
    unsigned refills=0;
    Input(std::string value,std::size_t block,std::size_t error):bytes(std::move(value)),chunk(block),fail(error){}
protected:
    int_type underflow() override {
        ++refills;
        if(position>=fail)throw std::runtime_error("controlled input failure");
        if(position>=bytes.size())return traits_type::eof();
        auto size=std::min({chunk,bytes.size()-position,fail-position});
        char* begin=bytes.data()+position;setg(begin,begin,begin+size);position+=size;
        return traits_type::to_int_type(*gptr());
    }
};
int main(){unsigned cases=0;for(std::size_t size:{0u,1u,7u,511u,512u,8191u,8192u,8193u,65536u,131073u}){
    std::string bytes;for(std::size_t i=0;i<size;++i)bytes.push_back(static_cast<char>(i%256));
    for(std::size_t chunk:{1u,7u,512u,8192u,65536u})for(std::size_t fail:{std::numeric_limits<std::size_t>::max(),std::size_t(0),std::size_t(1),std::size_t(511),std::size_t(8192)}){
        Input old_buffer(bytes,chunk,fail),new_buffer(bytes,chunk,fail);std::istream old_input(&old_buffer),new_input(&new_buffer);std::ostringstream old_output;old_output<<old_input.rdbuf();
        auto actual=nift::detail::read_stream_owned(new_input);assert(actual==old_output.str());assert(old_buffer.refills==new_buffer.refills);++cases;
    }
}std::cout<<"PASS "<<cases<<" binary/EOF/partial-error transfers and identical input refill counts\n";}
