#include "SourceView.h"
#include <cassert>
#include <utility>
using namespace nift::detail;
int main() {
    const std::string source="\tfirst\r\n    repeated\n\t repeated\n";
    auto root=SourceView::identity("case.n",source);
    assert(root.locate(1).line==1 && root.locate(1).column==2);
    assert(root.locate(1).source_line=="\tfirst");
    assert(root.locate(12).line==2 && root.locate(12).column==5);
    assert(root.slice(8,12).slice(4,8).locate(0).column==5);
    auto trimmed=root.slice(8,12).trim(std::string_view(source).substr(8,12));
    assert(trimmed.locate(0).line==2 && trimmed.locate(0).column==5);
    SourceBuilder wrapped(trimmed);
    wrapped.generated("$[",0); wrapped.copy("repeated",0,8); wrapped.generated("]",7);
    auto value=std::move(wrapped).finish();
    assert(value.text=="$[repeated]");
    assert(value.view.locate(0).column==5 && value.view.locate(2).column==5);
    assert(value.view.slice(2,8).locate(3).column==8);
    SourceBuilder dedent(root);
    dedent.copy(source,12,8);dedent.copy(source,20,1);dedent.copy(source,23,8);
    auto body=std::move(dedent).finish();
    assert(body.text=="repeated\nrepeated");
    assert(body.view.locate(0).line==2 && body.view.locate(9).line==3);
    assert(body.view.locate(9).column==3);
    SourceBuilder nested(body.view);
    nested.generated("<",9);nested.append(std::string_view(body.text).substr(9),body.view.slice(9));nested.generated(">",16);
    auto fragment=std::move(nested).finish();
    assert(fragment.view.slice(1,8).slice(0,0).locate(0).column==3);
    assert(fragment.view.slice(1,8).locate(7).line==3);
    assert(root.slice(root.size(),0).locate(0).line==4);
    auto empty=SourceView::identity("empty.n","");
    assert(empty.locate(0).line==1 && empty.locate(0).column==1);
    assert(!SourceView{}.locate(0).line);
    auto copy=value.view;value={};assert(copy.locate(2).line==2);
    assert(root.mapping_bytes()==0 && root.span_count()==1);
}
