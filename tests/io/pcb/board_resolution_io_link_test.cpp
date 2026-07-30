#include <optional>
#include <utility>

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

template <typename BoardArgument>
concept CanResolveBoard = requires(BoardArgument &&board, const volt::io::PartLibraryBundle &bundle,
                                   volt::BoardResolutionCapabilities capabilities) {
    volt::io::resolve_board(std::forward<BoardArgument>(board), bundle, capabilities);
};

static_assert(CanResolveBoard<volt::Board &>);
static_assert(!CanResolveBoard<volt::Board>);
static_assert(!CanResolveBoard<const volt::Board>);

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
