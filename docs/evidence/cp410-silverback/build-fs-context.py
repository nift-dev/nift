from pathlib import Path
import subprocess,json
p=Path(__file__).parent.resolve();s=Path('src/ParserTemplate.cpp').read_text();mark='            // Prepared dispatch for user callables: bind args, run the prepared';assert s.count(mark)==1
branch=r'''            // Research only: controlled standalone, plain paths, value operands.
            if(standalone_script_host_&&!resource_path_authority_.enforce_filesystem_root&&((name=="stat"&&args.size()==1)||((name=="move"||name=="copy")&&args.size()==2))){
                bool plain=true;for(const auto& v:args)if(!v.is_string()||v.string.empty()||v.string[0]=='~'||nift::detail::glob_has_magic(v.string))plain=false;
                if(plain){
                    struct ResolvedOperation {fs::path base;std::vector<fs::path> paths;};
                    ResolvedOperation context;context.base=fs::current_path();context.paths.reserve(args.size());
                    for(const auto& v:args){fs::path path(v.string);if(path.is_relative())path=context.base/path;context.paths.push_back(fs::absolute(path).lexically_normal());}
                    if(name=="stat"){
                        const auto info=inspect_path(context.paths[0]);if(info.error)return false;
                        auto value=nift::RuntimeValue::make_object();value["exists"]=nift::RuntimeValue(info.exists);if(info.exists){value["type"]=nift::RuntimeValue(info.type);if(info.type=="file")value["size"]=nift::RuntimeValue(static_cast<double>(info.size));}out=std::move(value);return true;
                    }
                    std::error_code error;const bool directory=fs::is_directory(context.paths[1],error);auto target=directory?context.paths[1]/context.paths[0].filename():context.paths[1];error.clear();
                    if(name=="move")fs::rename(context.paths[0],target,error);else fs::copy_file(context.paths[0],target,fs::copy_options::overwrite_existing,error);
                    if(error)return false;
                    out=nift::RuntimeValue(nullptr);return true;
                }
            }
'''
(p/'fs-context-ParserTemplate.cpp').write_text(s.replace(mark,branch+mark));cmds=json.loads((p/'string-direct-build.json').read_text());cmds=[[x.replace('string-direct','fs-context') for x in cmd] for cmd in cmds];(p/'fs-context-build.json').write_text(json.dumps(cmds,indent=2)+'\n')
for cmd in cmds:subprocess.run(cmd,check=True)
