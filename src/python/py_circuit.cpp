#include "py_circuit.hpp"

#include <optional>

#include <volt/io/parts/part_library_bundle.hpp>

namespace volt::python {
namespace {

class EmptyAssetResolver final : public volt::PartAssetResolver {
  public:
    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &) const override {
        return std::nullopt;
    }
};

[[nodiscard]] std::shared_ptr<const volt::io::PartLibraryBundle> empty_selected_bundle() {
    const auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"volt.python.design", "1", volt::PartLibrarySchemaVersion::V1}};
    const auto resolver = EmptyAssetResolver{};
    return std::make_shared<const volt::io::PartLibraryBundle>(
        volt::io::PartLibraryBundle::build(builder, {}, resolver));
}

} // namespace

PyCircuit::PyCircuit() : selected_part_bundle_{empty_selected_bundle()} {}

const volt::io::PartLibraryBundle &PyCircuit::selected_part_bundle() const noexcept {
    return *selected_part_bundle_;
}

} // namespace volt::python
