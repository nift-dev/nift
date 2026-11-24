#include "rt/infer_collection.h"
#include "rt/infer_mutators.h"
#include "rt/collection.h"
#include "rt/literal.h"
#include "rt/string.h"

#include <algorithm>
#include <charconv>
// rt-lint: cold-file compile-time collection callable-flow inference
namespace nift::rt::infer_detail {
namespace {
TyRef array_type(const TyRef& elem){auto t=std::make_shared<Ty>();t->kinds=kKindArray;
    t->elem=elem;
    t->tuple=elem&&elem->kinds==0&&!elem->any;return t;}
TyRef collection_value(const TyRef& value){return value;}
TyRef object_type(const TyRef& elem){auto t=std::make_shared<Ty>();t->kinds=kKindObject;
    t->elem=elem;return t;}
// The ordinary allocation classes still widen. This separate shape is valid
// only for a literal array with no aliasing use and a comparator used at this
// call alone. Comparator parameters can inspect scalar fields and stringify
// a record, but cannot expose or write a record.
TyRef isolated_sort_record(const Ast& ast, NodeId root, const Node& call, const Node& fn) {
    const NodeId recv_id = ast.at(call.a).a;
    const NodeId callback_id = ast.list(call)[0];
    const Node& receiver = ast.at(recv_id);
    if (receiver.kind != NodeKind::Ident || fn.count != 2 ||
        fn.flags & (kFlagAsync | kFlagVariadic | kFlagMethod | kFlagDeferredBody) ||
        ast.at(callback_id).kind != NodeKind::Ident) return {};
    const auto recv = ast.text(receiver);
    const auto name = ast.text(ast.at(fn.a));
    if (ast.text(ast.at(callback_id)) != name || recv == name) return {};
    const NodeId* params = ast.list(fn);
    const auto left = ast.text(ast.at(params[0])), right = ast.text(ast.at(params[1]));
    if (left == recv || right == recv || left == name || right == name ||
        (ast.at(params[0]).flags | ast.at(params[1]).flags) & kFlagVariadic) return {};
    NodeId declaration = kNoNode, literal = kNoNode;
    const Node& program = ast.at(root);
    for (std::uint32_t i = 0; i < program.count; ++i) {
        const Node& stmt = ast.at(ast.list(program)[i]);
        if (stmt.kind != NodeKind::Declare || ast.at(stmt.a).kind != NodeKind::Ident ||
            ast.text(ast.at(stmt.a)) != recv) continue;
        if (declaration != kNoNode || stmt.end >= call.begin) return {};
        declaration = stmt.a;
        literal = stmt.b;
    }
    if (literal == kNoNode) return {};
    const Node& array = ast.at(literal);
    if (array.kind != NodeKind::Array || array.count <= 16) return {};
    auto shape = std::make_shared<Ty>();
    shape->kinds = kKindObject;
    for (std::uint32_t i = 0; i < array.count; ++i) {
        const Node& object = ast.at(ast.list(array)[i]);
        if (object.kind != NodeKind::Object || !object.count) return {};
        std::unordered_set<std::string> fields;
        for (std::uint32_t k = 0; k < object.count; ++k) {
            const Node& prop = ast.at(ast.list(object)[k]);
            if (prop.a == kNoNode || prop.b == kNoNode) return {};
            const auto key = ast.text(ast.at(prop.a));
            if (key.size() < 3 || key.front() != '"' || key.back() != '"' ||
                key.find('\\') != std::string_view::npos) return {};
            const auto text = ast.text(ast.at(prop.b));
            std::int64_t value = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() ||
                value < -9007199254740991LL || value > 9007199254740991LL) return {};
            const std::string field(key.substr(1, key.size() - 2));
            if (!fields.insert(field).second) return {};
        }
        if (i == 0)
            for (const auto& key : fields) shape->fields.emplace(key, kinds_ty(kKindInt));
        else if (fields.size() != shape->fields.size() ||
                 std::any_of(fields.begin(), fields.end(),
                             [&](const auto& key) { return !shape->fields.count(key); })) return {};
    }
    struct Visit { NodeId id, parent, grandparent; bool comparator; };
    std::vector<Visit> pending{{root, kNoNode, kNoNode, false}};
    while (!pending.empty()) {
        const Visit item = pending.back();
        pending.pop_back();
        if (item.id == kNoNode) continue;
        const Node& node = ast.at(item.id);
        if (node.kind == NodeKind::Command || node.kind == NodeKind::Opaque ||
            node.kind == NodeKind::TemplateStmt || node.kind == NodeKind::StructDecl) return {};
        const bool comparator = item.comparator || item.id == fn.b;
        if (node.kind == NodeKind::Param || node.kind == NodeKind::Ident) {
            const auto word = ast.text(node);
            // Ignore a member key. It is data, not a binding or a use.
            const Node& parent = ast.at(item.parent);
            if ((parent.kind == NodeKind::Member || parent.kind == NodeKind::SafeMember) &&
                parent.b == item.id) continue;
            if (word == name && item.id != callback_id && item.id != fn.a) return {};
            const bool record = comparator && (word == left || word == right);
            const bool array_use = word == recv && item.id != declaration && item.id != recv_id;
            if (record || array_use) {
                if (parent.kind != NodeKind::Member || parent.a != item.id || item.grandparent == kNoNode)
                    return {};
                const auto field = ast.text(ast.at(parent.b));
                const Node& outer = ast.at(item.grandparent);
                const bool presentation = field == "stringify" && outer.kind == NodeKind::Call &&
                    outer.a == item.parent && outer.count == 0;
                if (!presentation &&
                    !(record && outer.kind == NodeKind::Binary && shape->fields.count(std::string(field))))
                    return {};
            }
        }
        for (NodeId child : {node.a, node.b, node.c})
            pending.push_back({child, item.id, item.parent, comparator});
        for (std::uint32_t i = 0; i < node.count; ++i)
            pending.push_back({ast.list(node)[i], item.id, item.parent, comparator});
    }
    return shape;
}
}
TyRef Inference::collection_callables(const TyRef& callee,const std::vector<TyRef>& values,bool& grew) {
    std::vector<TyRef> arguments;arguments.reserve(values.size());
    for(const TyRef& value:values)arguments.push_back(collection_value(value));
    const TyRef normal=call_callables(callee,arguments,grew);
    if(callee->any||(callee->kinds&kKindUnknown))return normal;
    TyRef out=empty_ty();
    for(int unit:callee->callables) {
        if(unit>=0&&static_cast<std::size_t>(unit)<plan_.functions.size())out=unite(out,returns_[unit]);
        else if(unit>static_cast<int>(plan_.functions.size())) {
            const auto& info=lambdas_[unit-plan_.functions.size()-1];
            out=unite(out,(ast_.at(info.node).flags&kFlagBlockBody)?info.collection_returns:info.returns);
        }
    }
    return out;
}
// An array method call's result (`recv` is an array; `call` the call's node, its allocation site).
// B4b 32 (review of ce0a9a44, N-1): reverse() and both sorts return their receiver
// (kCollectionResults, rt/infer_mutators.h), so their result is an alias: the method's result type
// (the receiver's elements, positions dropped by the reorder) with the receiver's own sites, so
// both name one site class and the one dependency mechanism covers it (a read of the result records
// its reader on the receiver's classes, a store through it notifies the receiver's readers). A
// receiver without a site (an earlier session statement's value, U6) gives a result without one,
// read and stored exactly like the receiver. Every other result is a new array or object (an
// allocation site at the call) or a value that carries its own sites already.
TyRef Inference::collection_call(const Node& n,NodeId call,const TyRef& recv,const std::vector<TyRef>& values,
                                 int id) {
    const TyRef result=collection_result(n,recv,values,id);
#if !(defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 38)  // A38: a new site (ce0a9a44, review N-1)
    if(collection_result_identity(static_cast<CollectionMethod>(id))==ResultIdentity::Receiver) {
        auto alias=std::make_shared<Ty>(*result);
        alias->sites=recv->sites;
        return alias;
    }
#endif
    return fresh(call,result);
}
TyRef Inference::collection_result(const Node& n,const TyRef& recv,const std::vector<TyRef>& values,int id) {
    using M=CollectionMethod;
    const auto m=static_cast<M>(id);
    TyRef element=element_of(recv,nullptr,false);
    // B06 (owner 2026-11-09 16:46, item 4): an empty element type is "holds nothing" when it is
    // proven: the empty literal's positional fact (B3 N1), or, since B4b's per-site contents, a
    // receiver read precisely from its allocation sites (none of unknown origin; an open or
    // collapsed class reads as anything) into which nothing was ever stored. B4b's site stores
    // drop the positional fact on the first mutator (`a := []; return a.pop()`), so the B3 rule
    // alone no longer sees it. Persistent sessions keep the B3 rule only.
#if !(defined(NIFT_B06_MUTANT) && NIFT_B06_MUTANT == 1)  // B06-1: only the B3 positional rule
    const bool stored_nothing=!session_&&!recv->any&&recv->elem&&!recv->sites.empty()&&
                              recv->sites.back()!=kOpenSite;
#else
    const bool stored_nothing=false;
#endif
    if(element->kinds==0&&!element->any&&!(recv->tuple&&recv->items.empty())&&!stored_nothing)element=any_ty();
    const NodeId receiver_id=ast_.at(n.a).a;
    result_.collection_elements[receiver_id]=element->kinds;
    if(element->legacy_text_number)result_.collection_text_numbers.insert(receiver_id);
    else result_.collection_text_numbers.erase(receiver_id);
    if(m<M::Map) {
        const auto receiver = ast_.at(n.a).a;
        // B4b (C1): the method's effect on the receiver's sites, through every alias.
        const SiteEffect effect = collection_effect(m);
        TyRef stored = empty_ty();
        if (effect == SiteEffect::StoreArg0 && !values.empty()) stored = values[0];
        if (effect == SiteEffect::StoreArg1 && values.size() > 1) stored = values[1];
        if (effect == SiteEffect::StoreSplice && values.size() > 2) stored = element_of(values[2], nullptr, false);
#if defined(NIFT_B3_MUTANT) && NIFT_B3_MUTANT == 11
        // D11 (B3 N1) follows positions into the sites: a script's non-append mutator keeps them.
        if (effect != SiteEffect::None && (session_ || effect == SiteEffect::StoreArg0 ||
                                           effect == SiteEffect::StoreArg1 || effect == SiteEffect::StoreSplice))
#else
        if (effect != SiteEffect::None)
#endif
            site_store(recv, stored, nullptr);
        if (m == M::Push && !values.empty()) add_element(receiver, values[0]);
        if (m == M::Insert && values.size() > 1) add_element(receiver, values[1]);
        if (m == M::Splice && values.size() > 2) add_element(receiver, element_of(values[2], nullptr, false));
        // Any mutation invalidates positional facts (B3 review N1: in scripts and
        // modules too, not only sessions: after `xs.reverse()` the instance that
        // was at xs[0] may be anywhere, so `xs[0]` is no longer a proven receiver).
#if defined(NIFT_B3_MUTANT) && NIFT_B3_MUTANT == 11
        if (session_ && m != M::Push && m != M::Insert && !(m == M::Splice && values.size() > 2))  // D11
#else
        // An empty literal has no positions to forget: invalidating it would turn
        // its proven "holds nothing" element into "unknown" (cp14b
        // script_return_kernel_error: `a := []; return a.pop()`).
        if (m != M::Push && m != M::Insert && !(m == M::Splice && values.size() > 2) &&
            (session_ || !(recv->tuple && recv->items.empty())))
#endif
            add_element(receiver, empty_ty());
        if (m == M::Sort && !values.empty()) {
            bool grew = false;
            TyRef argument = collection_value(element);
            if (!session_ && exact_callee(values[0]) && values[0]->callables.size() == 1 &&
                values[0]->callables.front() < plan_.functions.size()) {
                const NodeId fn = plan_.functions[values[0]->callables.front()];
                auto [proof, inserted] = sort_record_proofs_.try_emplace(receiver_id, fn, TyRef{});
                if (inserted || proof->second.first != fn)
                    proof->second = {fn, isolated_sort_record(ast_, root_, n, ast_.at(fn))};
                if (proof->second.second) argument = fresh(receiver_id, proof->second.second);
            }
            const TyRef order=call_callables(values[0], {argument, argument}, grew);
            if (tagged() && (order->kinds & kKindTagged)) result_.collection_inspections.insert(n.a);  // D-063
        }
        // D-063: a plain sort compares the elements (legacy: tag strings).
        if (m == M::Sort && values.empty() && tagged() && (element->kinds & kKindTagged))
            result_.collection_inspections.insert(n.a);
        if(m==M::Push||m==M::Insert){const std::size_t at=m==M::Push?0:1;
            return collection_value(at<values.size()?values[at]:any_ty());}
        if(m==M::Pop||m==M::Remove)return collection_value(element);
        if(m==M::Clear)return kinds_ty(kKindNull);
        return array_type(element);
    }
    for (std::size_t k=0; k<values.size(); k+=(m==M::SortBy&&values.size()>1?2:1)) {
        if(m==M::Reduce&&k>0)break;
        for(int unit:values[k]->callables)
            if(unit>static_cast<int>(plan_.functions.size())) {
                const NodeId lambda=lambdas_[unit-plan_.functions.size()-1].node;
                if(ast_.at(lambda).count==(m==M::Reduce?2u:1u))result_.collection_lambdas.insert(lambda);
            }
    }
    TyRef result=empty_ty();
    if(!values.empty()) {
        bool grew=false;
        if(m==M::SortBy){for(std::size_t k=0;k<values.size();k+=(values.size()==1?1:2))result=unite(result,
            collection_callables(values[k],{element},grew));}
        else if(m==M::Reduce&&values.size()>1){result=collection_callables(values[0],{values[1],element},grew);
            result=unite(result,values[1]);bool ignored=false;result=unite(result,
            collection_callables(values[0],{result,element},ignored));}
        else result=collection_callables(values[0],{element},grew);
    }
    // D-063: the key/selector callbacks' results are compared, hashed or
    // checked as keys (legacy: a collection result is its tag string).
    const bool keyed_callback=m==M::SortBy||m==M::GroupBy||m==M::IndexBy||m==M::UniqueBy||m==M::MinBy||
                              m==M::MaxBy||m==M::CountBy||m==M::GroupByEach;
    // group_by_each's callback returns an array of keys.
    const KindSet keys=m==M::GroupByEach?(result->kinds|element_of(result,nullptr,false)->kinds):result->kinds;
    if(keyed_callback&&tagged()&&(keys&kKindTagged))result_.collection_inspections.insert(n.a);
    switch(m) {
        case M::Map:return array_type(result);
        case M::Filter:case M::SortBy:case M::UniqueBy:return array_type(element);
        case M::Reduce:return collection_value(result);
        case M::Any:case M::All:return kinds_ty(kKindBool);
        case M::Find:return collection_value(unite(element,kinds_ty(kKindNull)));
        case M::FindIndex:case M::Count:return kinds_ty(kKindInt);
        case M::MinBy:case M::MaxBy:return collection_value(element);
        case M::Partition:{auto t=std::make_shared<Ty>();t->kinds=kKindObject;
            t->fields["matched"]=array_type(element);t->fields["unmatched"]=array_type(element);return t;}
        case M::IndexBy:return object_type(element);
        case M::CountBy:return object_type(kinds_ty(kKindInt));
        case M::GroupBy:case M::GroupByEach:return object_type(array_type(element));
        default:return any_ty();
    }
}
// D-063: map/set/stack methods. D-NEXT-refsem: a store through any binding
// widens the contents of every collection of that kind (contents()), since
// aliases share the object; the receiver's own type carries no contents.
TyRef Inference::contents(const TyRef& collection, bool keys, const std::string* name) const {
    if (collection->any) return (collection->kinds & kKindUnknown) ? any_ty() : top_ty();
    // Persistent sessions: an earlier statement's stores are not in this
    // walk, so a session read may be anything (sound; refused where it matters).
    if (session_ || contents_unknown_ || collection->sites.empty() ||
        collection->sites.back() == kOpenSite) return top_ty();  // B4b: of unknown origin too
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 7
    return empty_ty();  // R7: nothing is known to be stored (inference unsound under aliasing)
#endif
    TyRef out = empty_ty();
    for (std::uint32_t site : collection->sites) {
        const auto found = contents_.find(site);
        if (found == contents_.end()) continue;
        const SiteContents& c = found->second;
        if (keys) { out = unite(out, c.keys); continue; }
        out = unite(out, c.values);  // stored under a key that may be any
        if (name != nullptr) {
            const auto field = c.named.find(*name);
            if (field != c.named.end()) out = unite(out, field->second);
        } else {
            for (const auto& field : c.named) out = unite(out, field.second);
        }
    }
    return out;
}
void Inference::add_contents(const TyRef& collection, const TyRef& value, bool keys, const std::string* name) {
    if (collection->any) return;  // the compiler refuses such a receiver
    bool grew = false;
    if (collection->sites.empty() || collection->sites.back() == kOpenSite) {  // B4b: unknown origin too
        grew = !contents_unknown_;
        contents_unknown_ = true;
    }
    for (std::uint32_t site : collection->sites) {
        SiteContents& c = contents_[site];
        TyRef& slot = keys ? c.keys : name != nullptr ? c.named[*name] : c.values;
        grew = merge(slot, value) || grew;
    }
    if (!grew) return;
    self_changed_ = true;
    for (std::size_t u = 0; u <= plan_.functions.size() + lambdas_.size(); ++u) enqueue(u);
}
// The first argument of a map method when it is a plain string literal.
const std::string* Inference::literal_key(const Node& call, std::string& storage) const {
    if (call.count == 0) return nullptr;
    const Node& key = ast_.at(ast_.list(call)[0]);
    Value decoded;
    if (key.kind != NodeKind::String || key.count != 0 || !decode_string_literal(ast_.text(key), decoded))
        return nullptr;
    storage = std::string(string_view_of(decoded));
    return &storage;
}
// `map()`, `set()`, `stack()`: a new collection from allocation site `call`.
TyRef Inference::constructed(const Node& call, const TyRef& result) const {
    if (call.count != 0 || (result->kinds != kKindMap && result->kinds != kKindSet && result->kinds != kKindStack))
        return result;
    return collection_ty(result->kinds, static_cast<std::uint32_t>(&call - &ast_.at(0)));
}
TyRef Inference::set_members(const TyRef& set) const { return contents(set, false); }
TyRef Inference::map_keys(const TyRef& map) const { return contents(map, true); }
TyRef Inference::keyed_result(const Node& n,const TyRef& recv,const std::vector<TyRef>& values,
                              std::string_view method) {
    std::string storage;
    const std::string* key=recv->kinds==kKindMap?literal_key(n,storage):nullptr;
    const auto element=[&]{return contents(recv,false,key);};
    if(method=="stringify"||method=="prettify"||method=="highlight")return kinds_ty(kKindString);
    if(method=="size")return kinds_ty(kKindInt);
    if(method=="empty"||method=="contains")return kinds_ty(kKindBool);
    if(method=="clear")return kinds_ty(kKindNull);
    if(recv->kinds==kKindMap) {
        if(method=="set"){if(values.size()==2){add_contents(recv,values[0],true);add_contents(recv,values[1],false,key);
            return values[1];}return any_ty();}
        if(method=="get"||method=="remove")return element();
    } else if(recv->kinds==kKindSet) {
        if(method=="add"&&values.size()==1){add_contents(recv,values[0],false);return values[0];}
        if(method=="remove"&&values.size()==1)return values[0];
    } else if(recv->kinds==kKindStack) {
        if(method=="push"){if(values.size()==1){add_contents(recv,values[0],false);return values[0];}return any_ty();}
        if(method=="pop"||method=="top"||method=="front")return element();
    }
    return any_ty();
}
} // namespace nift::rt::infer_detail
