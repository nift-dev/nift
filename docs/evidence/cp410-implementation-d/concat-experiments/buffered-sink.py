from pathlib import Path
p=Path(__file__).resolve().parent
f=p/'prototype/src/ParserHelpers.cpp';s=f.read_text();a=s.index('    class OwnedBytes final:public std::streambuf {');b=s.index('    std::ostream output(&buffer);output<<input.rdbuf();return std::move(buffer.bytes);',a)
replacement='''    class OwnedBytes final:public std::streambuf {
        std::string bytes;
        std::size_t used() const { return pbase()?static_cast<std::size_t>(pptr()-pbase()):0; }
        void position(std::size_t count) {
            setp(bytes.data(),bytes.data()+bytes.size());
            while(count>static_cast<std::size_t>(std::numeric_limits<int>::max())){
                pbump(std::numeric_limits<int>::max());count-=static_cast<std::size_t>(std::numeric_limits<int>::max());
            }
            pbump(static_cast<int>(count));
        }
        void grow(std::size_t extra) {
            const auto count=used();
            if(extra>bytes.max_size()-count)throw std::length_error("stream text exceeds string limit");
            const auto required=count+extra;
            if(required>bytes.size()){
                const auto doubled=bytes.size()>bytes.max_size()/2?bytes.max_size():bytes.size()*2;
                bytes.resize(std::max(required,std::max(std::min<std::size_t>(512,bytes.max_size()),doubled)));
                position(count);
            }
        }
    public:
        std::string take(){bytes.resize(used());return std::move(bytes);}
    protected:
        std::streamsize xsputn(const char* data,std::streamsize size) override {
            if(size<=0)return 0;
            grow(static_cast<std::size_t>(size));
            const auto count=used();std::memcpy(bytes.data()+count,data,static_cast<std::size_t>(size));
            position(count+static_cast<std::size_t>(size));return size;
        }
        int_type overflow(int_type value) override {
            if(!traits_type::eq_int_type(value,traits_type::eof())){
                grow(1);*pptr()=traits_type::to_char_type(value);pbump(1);
            }
            return traits_type::not_eof(value);
        }
    } buffer;
'''
s=s[:a]+replacement+s[b:].replace('return std::move(buffer.bytes);','return buffer.take();',1)
# Explicit portable dependencies for the owned streambuf implementation.
s=s.replace('#include "ParserHelpers.h"','#include "ParserHelpers.h"\n#include <cstring>\n#include <limits>\n#include <stdexcept>')
f.write_text(s);(p/'ParserHelpers.cpp').write_text(s)
