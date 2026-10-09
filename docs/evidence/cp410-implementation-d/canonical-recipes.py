from pathlib import Path
p=Path(__file__).resolve().parent;s=(p/'ParserExpression.cpp').read_text();h=(p/'Parser.h').read_text()
needle='    bool execute_file_method('
fields='''    // Syntax-only canonical recipes, bounded by a conservative owned-memory
    // allowance. No values, bindings, cwd or source-view owners are retained.
    std::unordered_map<std::string,std::shared_ptr<const PreparedFilesystemOperation>> canonical_file_plans_;
    std::size_t canonical_file_plan_bytes_=0;
'''
assert needle in h;h=h.replace(needle,fields+needle,1)
start=s.index('        bool file_recipe_eligible=false;');end=s.index('        auto find_binding =',start)
block=s[start:end].replace('recipe->','operation_recipe->').replace('if(recipe&&','if(operation_recipe&&').replace('if (recipe &&','if (operation_recipe &&')
helper='''        const PreparedFilesystemOperation* operation_recipe=recipe;
        std::shared_ptr<const PreparedFilesystemOperation> operation_recipe_owner;
        if(!operation_recipe&&depth==0&&text.size()<=4096&&text.back()==')'){
            const auto open=text.find('(');
            const auto dot=text.find('.');
            const bool candidate=text.rfind("file(",0)==0||(open!=std::string::npos&&dot<open&&valid_binding_identifier(text.substr(0,dot)));
            if(candidate){
                auto found=canonical_file_plans_.find(text);
                if(found==canonical_file_plans_.end()){
                    operation_recipe_owner=prepare_filesystem_operation(text);
                    // Each pure operand plan is capped at 64 nodes/4 KiB.
                    // Charge worst-case repeated source ownership plus node,
                    // operand, hash-node and shared control-block allowance.
                    const std::size_t charge=text.size()*128+16384*(operation_recipe_owner?operation_recipe_owner->operands.size()+1:1);
                    if(charge<=4*1024*1024){
                        if(canonical_file_plans_.size()>=32||canonical_file_plan_bytes_+charge>4*1024*1024){canonical_file_plans_.clear();canonical_file_plan_bytes_=0;}
                        found=canonical_file_plans_.emplace(text,operation_recipe_owner).first;canonical_file_plan_bytes_+=charge;
                    }
                }
                if(found!=canonical_file_plans_.end())operation_recipe_owner=found->second;
                if(operation_recipe_owner&&(operation_recipe_owner->kind==PreparedFilesystemOperation::Kind::FileFactory||operation_recipe_owner->kind==PreparedFilesystemOperation::Kind::FileMethod))operation_recipe=operation_recipe_owner.get();
            }
        }
'''
# Clearing invalidates a previously obtained end iterator too. Reacquire it.
helper=helper.replace('canonical_file_plan_bytes_=0;}','canonical_file_plan_bytes_=0;found=canonical_file_plans_.end();}')
s=s[:start]+helper+block+s[end:]
(p/'Parser.h').write_text(h);(p/'ParserExpression.cpp').write_text(s)
print('Canonical-only FileValue fallback now reuses bounded syntax recipes')
