#pragma once

// OrderGuard (B, package ecosystem): an ordering comparison whose operand
// kinds the compiler cannot prove is compiled with a run-time kind check that
// raises legacy's fatal Error for the statement it is in. The context is set
// per statement; see compiler_order_guard.cpp for the contexts admitted.

#include "rt/ast.h"

#include <cstdint>

namespace nift::rt::detail {

// root: the expression whose comparisons may be guarded (through `&&`, `||`
// and `!` only); offset: legacy's report site, 0 for the comparison itself;
// wording: 0 plain, 1 a canonical `if` condition, 2 a canonical return value.
struct OrderContext {
    NodeId root = kNoNode;
    std::uint32_t offset = 0;
    int wording = 0;
};

template <typename Context>
class OrderScopeT {
public:
    OrderScopeT(Context& slot, Context value) : slot_(slot), saved_(slot) { slot_ = value; }
    ~OrderScopeT() { slot_ = saved_; }
    OrderScopeT(const OrderScopeT&) = delete;
    OrderScopeT& operator=(const OrderScopeT&) = delete;

private:
    Context& slot_;
    Context saved_;
};

} // namespace nift::rt::detail

namespace nift::rt::detail {
using OrderScope = OrderScopeT<OrderContext>;
} // namespace nift::rt::detail
