from pathlib import Path
p=Path(__file__).resolve().parent
for stage in ['certification','prototype']:
 f=p/stage/'src/ParserExpression.cpp';s=f.read_text()
 old='''            const auto open=text.find('(');
            const auto dot=text.find('.');
            const bool candidate=text.rfind("file(",0)==0||(open!=std::string::npos&&dot<open&&valid_binding_identifier(text.substr(0,dot))&&known_file_method(std::string_view(text).substr(dot+1,open-dot-1)));'''
 new='''            // Inspect only the call head. Scanning arbitrary argument/body text
            // here taxes unrelated named calls and callback factories.
            bool candidate=text.rfind("file(",0)==0;
            if(!candidate&&(std::isalpha(static_cast<unsigned char>(text.front()))||text.front()=='_')){
                std::size_t head=1;
                while(head<text.size()&&(std::isalnum(static_cast<unsigned char>(text[head]))||text[head]=='_'))++head;
                if(head<text.size()&&text[head]=='.'){
                    const auto method_begin=++head;
                    while(head<text.size()&&(std::isalnum(static_cast<unsigned char>(text[head]))||text[head]=='_'))++head;
                    candidate=head<text.size()&&text[head]=='('&&known_file_method(std::string_view(text).substr(method_begin,head-method_begin));
                }
            }'''
 assert old in s;s=s.replace(old,new);f.write_text(s)
