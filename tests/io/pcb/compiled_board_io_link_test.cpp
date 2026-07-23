#include <optional>

#include <volt/io/pcb/compiled_board.hpp>

namespace {

class EmptyAssetResolver final : public volt::PartAssetResolver {
  public:
    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &) const override {
        return std::nullopt;
    }
};

[[nodiscard]] volt::BoardCapabilityProfile profile() {
    return volt::BoardCapabilityProfile{
        "IO link contract",
        volt::BoardCapabilityProvenance{"Native fixture", "2026-07-23"},
        0.1,
        0.2,
        0.4,
        {}};
}

} // namespace

int main() {
    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"link.compiled", "1", volt::PartLibrarySchemaVersion::V1}};
    const auto bundle = volt::io::PartLibraryBundle::build(
        builder, std::span<const volt::PartKey>{}, EmptyAssetResolver{});
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit, volt::BoardName{"IO link contract"}};
    board.set_capability_profile(profile());
    auto result =
        volt::io::compile_board(circuit, board, bundle, volt::CompiledBoardCapabilities{profile()});
    if (!result.has_artifact() ||
        volt::io::compiled_board_format_name() != std::string_view{"volt.compiled-board"}) {
        return 1;
    }
    auto artifact = std::move(result).take_artifact();
    const auto bytes = volt::io::write_compiled_board(artifact);
    const auto reopened = volt::io::open_compiled_board(bytes);
    return reopened.bytes() == bytes && reopened.board_name().value() == "IO link contract" ? 0 : 1;
}
