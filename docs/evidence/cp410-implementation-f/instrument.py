from pathlib import Path
import re
p=Path(__file__).resolve().parent;stage=p/'prototype';h=stage/'src/RuntimeValue.h';s=h.read_text()
assert 'workload_object_event' not in s, 'Apply only to a fresh private E checkout'
declarations='''void workload_object_event(const void*,std::size_t,char) noexcept;
void workload_object_destroy(const void*) noexcept;
void workload_object_member(const void*,const void*,std::size_t) noexcept;
void workload_object_write(const void*) noexcept;
void workload_object_move(const void*,const void*,std::size_t) noexcept;
'''
s=s.replace('namespace nift {','namespace nift {\n'+declarations,1)
s=s.replace('    RuntimeValue() = default;','    RuntimeValue() = default;\n    ~RuntimeValue(){workload_object_destroy(this);}')
s=s.replace('            case RuntimeType::Object: object = other.object; break;','            case RuntimeType::Object: workload_object_event(&other,other.object.size(),\'c\'); object = other.object; workload_object_event(this,object.size(),\'o\'); break;')
s=s.replace('        if (this != &other) {','        if (this != &other) {\n            workload_object_write(this);',1)
s=s.replace('    RuntimeValue(RuntimeValue&&) noexcept = default;\n    RuntimeValue& operator=(RuntimeValue&&) noexcept = default;', '''    RuntimeValue(RuntimeValue&& other) noexcept
        :type(other.type),num(other.num),boolean(other.boolean),string(std::move(other.string)),array(std::move(other.array)),object(std::move(other.object)),bytes(std::move(other.bytes)),timer(std::move(other.timer)),error(std::move(other.error)){
        workload_object_move(this,&other,is_object()?object.size():0);
    }
    RuntimeValue& operator=(RuntimeValue&& other) noexcept {
        workload_object_write(this);
        type=other.type;num=other.num;boolean=other.boolean;string=std::move(other.string);array=std::move(other.array);object=std::move(other.object);bytes=std::move(other.bytes);timer=std::move(other.timer);error=std::move(other.error);
        workload_object_move(this,&other,is_object()?object.size():0);return *this;
    }''')
s=s.replace('bool runtime_equal(const RuntimeValue& left, const RuntimeValue& right);','''template<class Value> auto& workload_object_range(Value& value) {
    if constexpr(std::is_same_v<std::remove_cv_t<Value>,RuntimeValue>)workload_object_event(&value,value.object.size(),'i');
    return value.object;
}
bool runtime_equal(const RuntimeValue& left, const RuntimeValue& right);''')
s=s.replace('#include <utility>','#include <utility>\n#include <type_traits>');h.write_text(s)
cpp=stage/'src/RuntimeValue.cpp';s=cpp.read_text()
s=s.replace('    if (!is_object()) return nullptr;','    if (!is_object()) return nullptr;\n    workload_object_event(this,object.size(),\'r\');',1)
s=s.replace('    if (auto* member = find_member(key)) return *member;','    if (auto* member = find_member(key)){workload_object_member(member,this,object.size());return *member;}',1)
s=s.replace('    return object.back().second;','    workload_object_event(this,object.size(),\'n\');\n    workload_object_member(&object.back().second,this,object.size());\n    return object.back().second;',1)
s=s.replace('    return const_cast<RuntimeValue*>(static_cast<const RuntimeValue&>(*this).find_member(key));','    auto* member=const_cast<RuntimeValue*>(static_cast<const RuntimeValue&>(*this).find_member(key));\n    if(member)workload_object_member(member,this,object.size());\n    return member;',1)
s=s.replace('    return value;\n}\n\nnamespace {\nbool runtime_to_json_without_timer',"    if(value.is_object())workload_object_event(&value,value.object.size(),'o');\n    return value;\n}\n\nnamespace {\nbool runtime_to_json_without_timer",1)
cpp.write_text(s)
# Count initiation of actual range traversals, including early-return passes.
# No assumed full member visit count is inferred from the width.
matches=[]
for f in (stage/'src').glob('*.cpp'):
 if f.name not in {'RuntimeValue.cpp','Parser.cpp','ParserExpression.cpp','ParserTemplate.cpp','Ast.cpp'}:continue
 s=f.read_text()
 pattern=r'(for\s*\([^;{}\n]*?:\s*)([A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*)\.object(\s*\))'
 def replace(m):
  matches.append({'file':f.name,'range':m[2]});return m[1]+'nift::workload_object_range('+m[2]+')'+m[3]
 s=re.sub(pattern,replace,s);f.write_text(s)
(p/'range-sites.json').write_text(__import__('json').dumps(matches,indent=2)+'\n')
# Inline the observer in this private implementation object; never promote it.
cpp=stage/'src/RuntimeValue.cpp';s=cpp.read_text();observer=(p/'observer.inc').read_text();cpp.write_text(observer+'\n'+s)
print('Private object observer installed; range sites',len(matches))
