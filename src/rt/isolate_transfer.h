#pragma once
// CP23: a thread-neutral value transfer graph between isolates. Encoding owns
// plain STL data only (no Value, Atom id, HeapObject/handle pointer, or other
// thread-affine storage); decoding rebuilds values in the calling thread's
// atom table and heap accounting, preserving aliases and cycles by node id.
#include "rt/proto.h"
#include "rt/value.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace nift::rt {

struct ParallelTaskRecord;

enum class TransferStatus : std::uint8_t {
    Ok,
    RefusedFuture,
    RefusedHandle,
    ProtoUnavailable,
    InvalidGraph,
};

const char* transfer_status_name(TransferStatus status);

// Caller-owned mapping between immutable Proto objects and wire indices. The
// encoder calls index_of; the decoder calls proto_of. Both may be implemented
// without a running VM (tests use dummy protos).
class TransferProtoMap {
public:
    virtual ~TransferProtoMap() = default;
    virtual std::uint64_t domain() const noexcept { return 0; }
    virtual bool index_of(const Proto* proto, std::uint32_t& index) const = 0;
    virtual const Proto* proto_of(std::uint32_t index) const = 0;
};

class TransferGraph {
public:
    TransferGraph() = default;
    ~TransferGraph();
    TransferGraph(const TransferGraph&) = default;
    TransferGraph(TransferGraph&&) noexcept = default;
    TransferGraph& operator=(TransferGraph other) noexcept;
    const std::vector<std::shared_ptr<void>>& shared_backings() const noexcept { return shared_; }
    const std::vector<std::shared_ptr<ParallelTaskRecord>>& task_backings() const noexcept { return tasks_; }
    // Replaces the graph. On failure `error` is non-empty and the graph must
    // not be decoded.
    TransferStatus encode(const Value& root, const TransferProtoMap& protos, std::string& error);
    // Rebuilds a value graph owned by the calling thread. Never mutates this
    // graph; `root` is assigned only on success.
    TransferStatus decode(const TransferProtoMap& protos, Value& root, std::string& error) const;

    std::size_t storage_bytes() const noexcept; // bounded helper-admission estimate
    std::size_t node_count() const noexcept { return nodes_.size(); }
    bool empty() const noexcept { return nodes_.empty(); }
    void clear() noexcept;

private:
    enum class WireKind : std::uint8_t {
        Null, Bool, Int, Double, Decimal, String, Bytes, Array, Object, Struct, Error, Cell, Function,
        SharedHandle,  // CP23: registry id, never a live VM heap object
        TaskHandle,    // CP23: shared ParallelTaskRecord identity, never a FutureObject pointer
    };

    struct Node {
        WireKind kind = WireKind::Null;
        bool boolean = false;
        std::int64_t integer = 0;
        double number = 0;
        double approx = 0;
        std::string text;
        std::string spelling;
        std::string name;
        std::string message;
        std::string code;
        std::string category;
        std::string source;
        std::uint64_t line = 0;
        std::uint64_t column = 0;
        std::int64_t binding_type = 0;
        std::uint32_t proto_index = 0;
        std::uint64_t proto_domain = 0;
        std::uint32_t native_id = 0;
        std::uint64_t handle_id = 0;
        std::uint32_t handle_generation = 0;
        std::uint8_t handle_kind = 0;
        std::uint64_t task_id = 0;
        bool task_thread = false;
        bool has_proto = false;
        bool is_lambda = false;
        bool expression_body = false;
        bool selector_call_site = false;
        bool has_child = false;
        std::uint32_t child = 0;
        std::vector<std::uint8_t> bytes;
        std::vector<std::uint32_t> children;
        std::vector<std::pair<std::string, std::uint32_t>> fields;
    };

    class Encoder;
    class Decoder;

    std::vector<Node> nodes_;
    std::vector<std::shared_ptr<void>> shared_;
    // Shared completion records referenced by TaskHandle nodes. Holding a
    // strong reference here keeps a transferred handle valid even when the
    // source isolate's FutureObject has already gone away.
    std::vector<std::shared_ptr<ParallelTaskRecord>> tasks_;
    std::uint32_t root_ = 0;
    bool has_root_ = false;
};

} // namespace nift::rt
