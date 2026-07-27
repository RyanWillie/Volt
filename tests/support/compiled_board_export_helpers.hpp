#pragma once

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/pcb/compiled_board.hpp>

namespace volt::test {

class ExportFixtureAssetResolver final : public PartAssetResolver {
  public:
    void add(const PartAssetReference &reference, std::string bytes) {
        assets_.insert_or_assign(key(reference), std::move(bytes));
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const PartAssetReference &reference) const override {
        const auto match = assets_.find(key(reference));
        return match == assets_.end() ? std::nullopt : std::optional{match->second};
    }

  private:
    [[nodiscard]] static std::string key(const PartAssetReference &reference) {
        return std::to_string(static_cast<unsigned int>(reference.kind())) + ":" + reference.key();
    }

    std::map<std::string, std::string> assets_;
};

struct ExportFixturePart {
    ComponentSpec component;
    PhysicalPart physical;
    FootprintDefinition footprint;
    PartKey key;
    std::optional<std::string> model_bytes = std::nullopt;
};

struct ExportFixtureLibrary {
    io::PartLibraryBundle bundle;
    std::vector<PartKey> keys;
};

[[nodiscard]] inline std::vector<PartFootprintPad>
export_fixture_part_pads(const FootprintDefinition &footprint) {
    auto result = std::vector<PartFootprintPad>{};
    result.reserve(footprint.pad_count());
    for (const auto &pad : footprint.pads()) {
        result.emplace_back(pad.label(), pad.position().x_mm(), pad.position().y_mm(),
                            pad.size().width_mm(), pad.size().height_mm());
    }
    return result;
}

[[nodiscard]] inline ExportFixtureLibrary
make_export_fixture_library(std::vector<ExportFixturePart> parts) {
    auto owner = Circuit{};
    auto definitions = std::vector<ComponentDefId>{};
    definitions.reserve(parts.size());
    for (const auto &part : parts) {
        definitions.push_back(owner.define_component(part.component));
    }

    auto builder =
        PartLibraryBuilder{PartLibraryIdentity{"test.export", "1", PartLibrarySchemaVersion::V1}};
    auto resolver = ExportFixtureAssetResolver{};
    auto keys = std::vector<PartKey>{};
    keys.reserve(parts.size());

    for (auto index = std::size_t{0}; index < parts.size(); ++index) {
        const auto &input = parts[index];
        const auto &definition = owner.get(definitions[index]);
        const auto footprint_bytes = io::write_footprint_asset(input.footprint);
        const auto footprint_reference =
            PartAssetReference{PartAssetKind::Footprint,
                               "footprint:" + input.physical.footprint().library() + "/" +
                                   input.physical.footprint().name(),
                               sha256_content_hash(footprint_bytes)};
        auto model_reference = std::optional<PartModel3DReference>{};
        auto model_asset = std::optional<PartAssetReference>{};
        if (input.physical.model_3d().has_value()) {
            if (!input.model_bytes.has_value()) {
                throw KernelArgumentError{
                    ErrorCode::InvalidArgument,
                    "Export fixture part with a 3D model must provide its exact bytes"};
            }
            const auto &model = *input.physical.model_3d();
            model_asset.emplace(PartAssetKind::Model3D,
                                "model:" + model.format() + "/" + model.file_name(),
                                sha256_content_hash(*input.model_bytes));
            model_reference.emplace(model.format(), model.file_name(), model_asset->digest(),
                                    model.translation_mm(), model.rotation_deg());
        }

        auto pin_terminal_mappings = std::vector<PinPackageTerminalMapping>{};
        auto terminal_pad_mappings = std::vector<PackageTerminalPadMapping>{};
        pin_terminal_mappings.reserve(input.physical.pin_pad_mappings().size());
        terminal_pad_mappings.reserve(input.physical.pin_pad_mappings().size());
        for (const auto &mapping : input.physical.pin_pad_mappings()) {
            const auto pin = std::ranges::find(definition.pins(), mapping.pin());
            if (pin == definition.pins().end()) {
                throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                       "Export fixture mapping references a foreign pin"};
            }
            const auto pin_index =
                static_cast<std::size_t>(std::ranges::distance(definition.pins().begin(), pin));
            const auto terminal = PackageTerminalKey{mapping.pad()};
            pin_terminal_mappings.emplace_back(definition.contract().pin_keys().at(pin_index),
                                               std::vector{PackageTerminalKey{mapping.pad()}});
            terminal_pad_mappings.emplace_back(terminal,
                                               std::vector{FootprintPadKey{mapping.pad()}});
        }

        builder.add_component(input.component)
            .add_part(PartDefinition{
                definition,
                PartIdentity{"test.export", input.key.value(), "1"},
                ElectricalRecordSet{definition.pins().size()},
                std::move(pin_terminal_mappings),
                {},
                PartProvenance{},
                {},
                OrderablePart{
                    input.physical.manufacturer_part(), input.physical.package(),
                    HashedFootprintReference{input.physical.footprint(),
                                             footprint_reference.digest()},
                    export_fixture_part_pads(input.footprint), std::move(terminal_pad_mappings),
                    input.physical.approved_alternate_mpns(), std::move(model_reference)}});
        resolver.add(footprint_reference, footprint_bytes);
        if (model_asset.has_value()) {
            resolver.add(*model_asset, *input.model_bytes);
        }
        keys.push_back(input.key);
    }

    return ExportFixtureLibrary{
        io::PartLibraryBundle::build(builder, keys, resolver),
        std::move(keys),
    };
}

[[nodiscard]] inline BoardCapabilityProfile export_fixture_profile() {
    return BoardCapabilityProfile{"CompiledBoard export fixture",
                                  BoardCapabilityProvenance{"Native test fixture", "2026-07-24"},
                                  0.1,
                                  0.2,
                                  0.4,
                                  {}};
}

[[nodiscard]] inline CompiledBoard
compile_export_fixture(const Circuit &circuit, Board board, const io::PartLibraryBundle &bundle,
                       std::vector<BoardAssetCapability> additional_capabilities = {}) {
    board.set_capability_profile(export_fixture_profile());
    auto result = io::compile_board(
        circuit, board, bundle,
        CompiledBoardCapabilities{export_fixture_profile(), std::move(additional_capabilities)});
    if (!result.has_artifact()) {
        throw KernelLogicError{ErrorCode::InvalidState,
                               result.failure().has_value()
                                   ? result.failure()->message()
                                   : "CompiledBoard export fixture compilation failed"};
    }
    return std::move(result).take_artifact();
}

} // namespace volt::test
