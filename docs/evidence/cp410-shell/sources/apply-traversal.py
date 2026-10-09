from pathlib import Path
p=Path('src/ParserHelpers.cpp');s=p.read_text();a='sort_glob_paths(entries, [](const auto& entry) -> const fs::path& { return entry.path(); });';assert s.count(a)==2;s=s.replace(a,'/* Final glob ordering covers entries from every directory. */');p.write_text(s)
p=Path('src/ParserExpression.cpp');s=p.read_text();a='if(glob_has_magic(raw)){auto matches=glob_expand(p);const fs::path base=standalone_script_host_?fs::current_path():host_.root();const bool absolute=fs::path(raw).is_absolute();for(const auto&m:matches){std::error_code rec;auto shown=absolute?m:fs::relative(m,base,rec);out.array.emplace_back((rec?m:shown).generic_string());}return true;}'
b='''if(glob_has_magic(raw)){
                auto matches=glob_expand(p);
                const fs::path base=standalone_script_host_?fs::current_path():host_.root();
                const bool absolute=fs::path(raw).is_absolute();
                std::error_code base_error;
                fs::path canonical_base;
                // relative() canonicalizes both operands. Resolve the common
                // base once, while still resolving every match's symlinks.
                if(!absolute&&!matches.empty())canonical_base=fs::weakly_canonical(base,base_error);
                for(const auto& m:matches){
                    std::error_code rec=base_error;
                    fs::path shown=m;
                    if(!absolute&&!rec){
                        auto canonical_match=fs::weakly_canonical(m,rec);
                        if(!rec)shown=canonical_match.lexically_relative(canonical_base);
                    }
                    out.array.emplace_back((rec?m:shown).generic_string());
                }
                return true;
            }'''
assert s.count(a)==1;p.write_text(s.replace(a,b))
p=Path('tests/v49_glob_key_guard.py');s=p.read_text().replace("int(m[1])==2*n","int(m[1])==n").replace("{2*n} ordering-key conversions","{n} ordering-key conversions");p.write_text(s)
print('Applied traversal candidate; no save/dispatch/architecture changes')
