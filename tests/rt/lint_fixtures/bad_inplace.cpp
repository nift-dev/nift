// Tooling review F1 (lint1/src/rt/probe.cpp): the in-place tier's primitives,
// renamed by component 2, each unannotated on a published value; and an alias
// and a using-declaration of the class. Every line naming Construction fires.
namespace nift::rt {
void p1(const build::InPlaceKey& key, Value& published) {
    Construction::array_set(key, published, 0, Value::integer(1));
    auto& raw = Construction::array_storage(key, published);
    raw.clear();
    Value* slot = Construction::object_slot(key, published, Atom{1});
    *slot = Value();
    Construction::map_set(key, published, Value::integer(1), Value::integer(2));
    Construction::set_add(key, published, Value::integer(1));
    Construction::stack_push(key, published, Value::integer(1));
    Construction::array_push(key, published, Value::integer(1));
    using C = Construction;
    C::array_set(key, published, 0, Value::integer(1));
    Value fresh = make_array(1);
    // rt-lint: storage-ok fresh, the value this function just made
    Construction::array_push(key, fresh, Value::integer(1));
    // rt-lint: storage-ok fresh, an annotation that covers nothing the lint checks
    fresh = Value();
}
}  // namespace nift::rt
