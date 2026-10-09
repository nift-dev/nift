from pathlib import Path
p=Path(__file__).resolve().parent
for name in ['ParserExpression.cpp','ParserTemplate.cpp']:
 f=p/'prototype/src'/name;s=f.read_text();old='d=render_expression_value(v);if('
 assert s.count(old)==1,(name,s.count(old));s=s.replace(old,'d=v.is_string()?std::move(v.string):render_expression_value(v);if(',1);f.write_text(s)
print('Move validated temporary strings in both existing FileValue write paths')
