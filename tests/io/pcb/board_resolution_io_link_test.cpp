#include <optional>

#include <volt/io/pcb/board_resolution.hpp>

namespace {

class EmptyAssetResolver final : public volt::PartAssetResolver {
  public:
    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &) const override {
        return std::nullopt;
    }
};

} // namespace

int main() {
    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"link.contract", "1", volt::PartLibrarySchemaVersion::V1}};
    const auto bundle = volt::io::PartLibraryBundle::build(builder, std::vector<volt::PartKey>{},
                                                           EmptyAssetResolver{});
    const auto circuit = volt::Circuit{};
    const auto board = volt::Board{circuit, volt::BoardName{"IO link contract"}};
    const auto resolution =
        volt::io::resolve_board(board, bundle, volt::BoardResolutionCapabilities{std::nullopt});

    return resolution.closure_digest() == bundle.digest() ? 0 : 1;
}
