#include "project_bundle_v2_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <volt/circuit/bom/bom.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/content_hash.hpp>
#include <volt/io/assembly/cpl_writer.hpp>
#include <volt/io/bom/bom_writer.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/parts/part_definition_reader.hpp>
#include <volt/io/parts/part_definition_writer.hpp>
#include <volt/io/pcb/board_scene.hpp>
#include <volt/io/pcb/compiled_board.hpp>
#include <volt/io/pcb/pcb_reader.hpp>
#include <volt/io/pcb/pcb_svg_writer.hpp>
#include <volt/io/pcb/pcb_writer.hpp>
#include <volt/io/schematic/schematic_reader.hpp>
#include <volt/io/schematic/schematic_svg_writer.hpp>
#include <volt/io/schematic/schematic_writer.hpp>
#include <volt/library/part_library.hpp>
#include <volt/pcb/compiled/board_consumers.hpp>

#include "project_bundle_v2_contract.hpp"

namespace volt::io::v2_open {

[[nodiscard]] const ComponentDefinition &component_for_part(const ArtifactDescriptor &part,
                                                            const LibraryDecoded &decoded) {
    for (const auto &dependency : part.dependencies()) {
        if (dependency.artifact().kind() != ArtifactKind::ComponentDefinition) {
            continue;
        }
        const auto key = detail::project_bundle_v2_artifact_key(dependency.artifact());
        const auto match = decoded.component_documents.find(key);
        require(match != decoded.component_documents.end(),
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "selected part component dependency is not decoded");
        return match->second->get(ComponentDefId{0});
    }
    fail(ProjectBundleOpenErrorCode::OwnershipViolation,
         "selected part has no vendored component dependency");
}

void decode_library_artifacts(ProjectBundleStorage &storage, LibraryDecoded &decoded) {
    for (auto index = std::size_t{0}; index < storage.v2_artifacts.size(); ++index) {
        const auto &artifact = storage.v2_artifacts[index];
        if (artifact.descriptor.kind() != ArtifactKind::ComponentDefinition) {
            continue;
        }
        try {
            auto document = std::make_unique<Circuit>(read_logical_circuit_text(artifact.bytes));
            require(write_logical_circuit(*document) == artifact.bytes,
                    ProjectBundleOpenErrorCode::ModelDecodeFailure,
                    "component definition document is not owner-canonical");
            require(document->all<ComponentDefId>().size() == 1U &&
                        document->all<ComponentId>().size() == 0U &&
                        document->all<NetId>().size() == 0U,
                    ProjectBundleOpenErrorCode::ModelDecodeFailure,
                    "component definition document is not a single-definition logical document");
            const auto &component = document->get(ComponentDefId{0});
            const auto &owner = std::get<LibraryComponentRef>(artifact.descriptor.id().owner());
            require(component.contract().key() == owner.component_key,
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "component contract key disagrees with its owner");
            require(component.content_identity() == owner.component_digest,
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "component content identity disagrees with its owner");
            decoded.component_documents.emplace(
                detail::project_bundle_v2_artifact_key(artifact.descriptor.id()),
                std::move(document));
        } catch (const ProjectBundleOpenError &) {
            throw;
        } catch (const std::exception &error) {
            fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                 "component definition decode failed: " + std::string{error.what()});
        }
    }

    for (const auto &artifact : storage.v2_artifacts) {
        if (artifact.descriptor.kind() == ArtifactKind::SymbolDefinition) {
            try {
                const auto symbol = read_symbol_definition_text(artifact.bytes);
                const auto &owner =
                    std::get<LibraryAttachmentRef>(artifact.descriptor.id().owner());
                const auto owner_prefix = "symbol:" + symbol.name() + "@";
                require(write_symbol_definition(symbol) == artifact.bytes &&
                            owner.key.starts_with(owner_prefix) &&
                            owner.key.size() > owner_prefix.size() &&
                            owner.content_digest == artifact.descriptor.content_digest(),
                        ProjectBundleOpenErrorCode::OwnershipViolation,
                        "symbol payload identity or canonical bytes disagree with its owner");
            } catch (const ProjectBundleOpenError &) {
                throw;
            } catch (const std::exception &error) {
                fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                     "symbol definition decode failed: " + std::string{error.what()});
            }
        } else if (artifact.descriptor.kind() == ArtifactKind::FootprintDefinition) {
            try {
                auto footprint = read_footprint_asset(artifact.bytes);
                require(write_footprint_asset(footprint) == artifact.bytes,
                        ProjectBundleOpenErrorCode::ModelDecodeFailure,
                        "footprint definition is not owner-canonical");
                const auto &owner =
                    std::get<LibraryAttachmentRef>(artifact.descriptor.id().owner());
                const auto expected_key =
                    "footprint:" + footprint.ref().library() + "/" + footprint.ref().name();
                require(owner.key == expected_key &&
                            owner.content_digest == artifact.descriptor.content_digest(),
                        ProjectBundleOpenErrorCode::OwnershipViolation,
                        "footprint payload identity disagrees with its owner");
                decoded.footprints.emplace(
                    detail::project_bundle_v2_artifact_key(artifact.descriptor.id()),
                    std::move(footprint));
            } catch (const ProjectBundleOpenError &) {
                throw;
            } catch (const std::exception &error) {
                fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                     "footprint definition decode failed: " + std::string{error.what()});
            }
        } else if (artifact.descriptor.kind() == ArtifactKind::GlbAsset) {
            require(artifact.bytes.size() >= 12U && artifact.bytes.substr(0U, 4U) == "glTF" &&
                        static_cast<unsigned char>(artifact.bytes[4]) == 2U &&
                        artifact.bytes[5] == '\0' && artifact.bytes[6] == '\0' &&
                        artifact.bytes[7] == '\0',
                    ProjectBundleOpenErrorCode::ModelDecodeFailure,
                    "GLB asset is not a version-2 binary glTF payload");
            const auto &owner = std::get<LibraryAssetRef>(artifact.descriptor.id().owner());
            require(owner.content_digest == artifact.descriptor.content_digest(),
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "GLB content identity disagrees with its owner");
        }
    }

    for (const auto &artifact : storage.v2_artifacts) {
        if (artifact.descriptor.kind() != ArtifactKind::PartDefinition) {
            continue;
        }
        try {
            const auto &owner = std::get<LibraryPartRef>(artifact.descriptor.id().owner());
            const auto &component = component_for_part(artifact.descriptor, decoded);
            auto part = std::make_unique<PartDefinition>(
                read_part_definition_text(artifact.bytes, component));
            require(write_part_definition(*part) == artifact.bytes &&
                        part->identity().namespace_name() == owner.library_namespace() &&
                        part->identity().version() == owner.library_version() &&
                        part->identity().name() == owner.part_key().value() &&
                        part->content_identity() == owner.part_digest() &&
                        part->implemented_component() == component.content_identity(),
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "part payload identity or implemented component disagrees with its owner");
            decoded.parts.emplace(detail::project_bundle_v2_artifact_key(artifact.descriptor.id()),
                                  std::move(part));
        } catch (const ProjectBundleOpenError &) {
            throw;
        } catch (const std::exception &error) {
            fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                 "part definition decode failed: " + std::string{error.what()});
        }
    }
}

[[nodiscard]] std::size_t circuit_for_design(const ProjectBundleStorage &storage,
                                             const DesignKey &design) {
    const auto match =
        std::ranges::find(storage.v2_circuits, design, &ProjectBundleStorage::V2Circuit::design);
    require(match != storage.v2_circuits.end(), ProjectBundleOpenErrorCode::OwnershipViolation,
            "projection names a missing logical Circuit owner");
    return static_cast<std::size_t>(match - storage.v2_circuits.begin());
}

void decode_project_models(ProjectBundleStorage &storage) {
    for (auto index = std::size_t{0}; index < storage.v2_artifacts.size(); ++index) {
        const auto &artifact = storage.v2_artifacts[index];
        if (artifact.descriptor.kind() != ArtifactKind::LogicalModel) {
            continue;
        }
        try {
            auto model = std::make_unique<Circuit>(read_logical_circuit_text(artifact.bytes));
            require(write_logical_circuit(*model) == artifact.bytes,
                    ProjectBundleOpenErrorCode::ModelDecodeFailure,
                    "logical model is not owner-canonical");
            storage.v2_circuits.push_back(ProjectBundleStorage::V2Circuit{
                std::get<LogicalArtifactIdentity>(artifact.descriptor.id().owner()).design, index,
                std::move(model)});
        } catch (const ProjectBundleOpenError &) {
            throw;
        } catch (const std::exception &error) {
            fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                 "logical model decode failed: " + std::string{error.what()});
        }
    }
    require(!storage.v2_circuits.empty(), ProjectBundleOpenErrorCode::MissingEntry,
            "v2 graph has no logical root");
    std::ranges::sort(storage.v2_circuits, {},
                      [](const auto &value) { return value.design.value(); });

    for (auto index = std::size_t{0}; index < storage.v2_artifacts.size(); ++index) {
        const auto &artifact = storage.v2_artifacts[index];
        if (artifact.descriptor.kind() == ArtifactKind::SchematicModel) {
            const auto &owner =
                std::get<SchematicArtifactIdentity>(artifact.descriptor.id().owner());
            const auto circuit = circuit_for_design(storage, owner.design);
            try {
                auto model = std::make_unique<Schematic>(
                    read_schematic_text(artifact.bytes, *storage.v2_circuits[circuit].model));
                require(write_schematic(*model) == artifact.bytes,
                        ProjectBundleOpenErrorCode::ModelDecodeFailure,
                        "Schematic is not owner-canonical");
                storage.v2_schematics.push_back(ProjectBundleStorage::V2Schematic{
                    owner.design, owner.schematic, index, circuit, std::move(model)});
            } catch (const ProjectBundleOpenError &) {
                throw;
            } catch (const std::exception &error) {
                fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                     "Schematic decode failed: " + std::string{error.what()});
            }
        } else if (artifact.descriptor.kind() == ArtifactKind::BoardModel) {
            const auto &owner = std::get<BoardArtifactIdentity>(artifact.descriptor.id().owner());
            const auto circuit = circuit_for_design(storage, owner.design);
            try {
                auto model = std::make_unique<Board>(
                    read_pcb_board_text(*storage.v2_circuits[circuit].model, artifact.bytes));
                require(write_pcb_board(*model, FootprintLibrary{}) == artifact.bytes &&
                            model->name() == owner.board,
                        ProjectBundleOpenErrorCode::OwnershipViolation,
                        "Board payload is not canonical or disagrees with its named owner");
                storage.v2_boards.push_back(ProjectBundleStorage::V2Board{
                    owner.design, owner.board, index, circuit, std::move(model)});
            } catch (const ProjectBundleOpenError &) {
                throw;
            } catch (const std::exception &error) {
                fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                     "Board decode failed: " + std::string{error.what()});
            }
        }
    }
    std::ranges::sort(storage.v2_schematics, [](const auto &left, const auto &right) {
        return std::tie(left.design.value(), left.schematic.value()) <
               std::tie(right.design.value(), right.schematic.value());
    });
    std::ranges::sort(storage.v2_boards, [](const auto &left, const auto &right) {
        return std::tie(left.design.value(), left.board.value()) <
               std::tie(right.design.value(), right.board.value());
    });
}

void decode_compiled_and_scenes(ProjectBundleStorage &storage) {
    for (auto index = std::size_t{0}; index < storage.v2_artifacts.size(); ++index) {
        const auto &artifact = storage.v2_artifacts[index];
        if (artifact.descriptor.kind() != ArtifactKind::CompiledBoard) {
            continue;
        }
        try {
            auto model = std::make_unique<CompiledBoard>(open_compiled_board(artifact.bytes));
            require(model->identity() ==
                            std::get<CompiledBoardIdentity>(artifact.descriptor.id().owner()) &&
                        write_compiled_board(*model) == artifact.bytes,
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "CompiledBoard identity or owner-canonical bytes disagree with the graph");
            storage.v2_compiled_boards.push_back(
                ProjectBundleStorage::V2CompiledBoard{index, std::move(model)});
        } catch (const ProjectBundleOpenError &) {
            throw;
        } catch (const std::exception &error) {
            fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                 "CompiledBoard decode failed: " + std::string{error.what()});
        }
    }

    for (auto index = std::size_t{0}; index < storage.v2_artifacts.size(); ++index) {
        const auto &artifact = storage.v2_artifacts[index];
        if (artifact.descriptor.kind() != ArtifactKind::BoardScene) {
            continue;
        }
        const auto &owner = std::get<BoardSceneArtifactIdentity>(artifact.descriptor.id().owner());
        const auto compiled =
            std::ranges::find_if(storage.v2_compiled_boards, [&](const auto &candidate) {
                return candidate.model->identity() == owner.compiled_board;
            });
        require(compiled != storage.v2_compiled_boards.end(),
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "BoardScene has no exact CompiledBoard owner");
        try {
            auto model =
                std::make_unique<BoardScene>(read_board_scene(artifact.bytes, *compiled->model));
            storage.v2_board_scenes.push_back(ProjectBundleStorage::V2BoardScene{
                index, static_cast<std::size_t>(compiled - storage.v2_compiled_boards.begin()),
                std::move(model)});
        } catch (const ProjectBundleOpenError &) {
            throw;
        } catch (const std::exception &error) {
            fail(ProjectBundleOpenErrorCode::ModelDecodeFailure,
                 "BoardScene decode failed: " + std::string{error.what()});
        }
    }
}

[[nodiscard]] std::set<std::string> refs_for_kinds(const ProjectBundleStorage &storage,
                                                   std::initializer_list<ArtifactKind> kinds) {
    auto result = std::set<std::string>{};
    for (const auto &artifact : storage.v2_artifacts) {
        if (std::ranges::find(kinds, artifact.descriptor.kind()) != kinds.end()) {
            result.insert(detail::project_bundle_v2_artifact_key(artifact.descriptor.id()));
        }
    }
    return result;
}

void require_exact_dependencies(const ArtifactDescriptor &artifact,
                                const std::set<std::string> &expected, std::string_view label) {
    require(dependency_keys(artifact) == expected, ProjectBundleOpenErrorCode::OwnershipViolation,
            std::string{label} + " direct dependency set is missing or extraneous");
}

[[nodiscard]] bool is_library_artifact(ArtifactKind kind) {
    return kind == ArtifactKind::ComponentDefinition || kind == ArtifactKind::PartDefinition ||
           kind == ArtifactKind::SymbolDefinition || kind == ArtifactKind::FootprintDefinition ||
           kind == ArtifactKind::GlbAsset;
}

void verify_library_reachability(const ProjectBundleStorage &storage,
                                 const std::map<std::string, std::size_t> &index) {
    auto reachable = std::set<std::string>{};
    auto pending = std::vector<std::size_t>{};
    for (auto artifact = std::size_t{0}; artifact < storage.v2_artifacts.size(); ++artifact) {
        if (storage.v2_artifacts[artifact].descriptor.kind() == ArtifactKind::LogicalModel) {
            pending.push_back(artifact);
        }
    }
    while (!pending.empty()) {
        const auto artifact = pending.back();
        pending.pop_back();
        for (const auto &dependency : storage.v2_artifacts[artifact].descriptor.dependencies()) {
            const auto key = detail::project_bundle_v2_artifact_key(dependency.artifact());
            if (!reachable.insert(key).second) {
                continue;
            }
            const auto match = index.find(key);
            require(match != index.end(), ProjectBundleOpenErrorCode::MissingEntry,
                    "reachable library dependency is absent from the graph");
            pending.push_back(match->second);
        }
    }
    for (const auto &artifact : storage.v2_artifacts) {
        if (is_library_artifact(artifact.descriptor.kind())) {
            require(reachable.contains(
                        detail::project_bundle_v2_artifact_key(artifact.descriptor.id())),
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "vendored library artifact is outside the logical dependency closure");
        }
    }
}

void verify_owner_graph(ProjectBundleStorage &storage, const LibraryDecoded &decoded,
                        const std::map<std::string, std::size_t> &index) {
    const auto evaluated =
        refs_for_kinds(storage, {ArtifactKind::LogicalModel, ArtifactKind::SchematicModel,
                                 ArtifactKind::BoardModel});
    for (const auto &artifact : storage.v2_artifacts) {
        const auto &descriptor_value = artifact.descriptor;
        switch (descriptor_value.kind()) {
        case ArtifactKind::SchematicModel: {
            const auto &owner = std::get<SchematicArtifactIdentity>(descriptor_value.id().owner());
            require_exact_dependencies(
                descriptor_value,
                {detail::project_bundle_v2_artifact_key(
                    ArtifactId{ArtifactKind::LogicalModel, LogicalArtifactIdentity{owner.design}})},
                "Schematic");
            break;
        }
        case ArtifactKind::BoardModel: {
            const auto &owner = std::get<BoardArtifactIdentity>(descriptor_value.id().owner());
            require_exact_dependencies(
                descriptor_value,
                {detail::project_bundle_v2_artifact_key(
                    ArtifactId{ArtifactKind::LogicalModel, LogicalArtifactIdentity{owner.design}})},
                "Board");
            break;
        }
        case ArtifactKind::Diagnostics:
        case ArtifactKind::ProjectTests:
            require_exact_dependencies(descriptor_value, evaluated, "project report");
            break;
        case ArtifactKind::ComponentDefinition: {
            const auto key = detail::project_bundle_v2_artifact_key(descriptor_value.id());
            const auto &component = decoded.component_documents.at(key)->get(ComponentDefId{0});
            auto expected = std::set<std::string>{};
            const auto &owner = std::get<LibraryComponentRef>(descriptor_value.id().owner());
            for (const auto &symbol : component.schematic_symbols()) {
                if (symbol.variant() != "default") {
                    continue;
                }
                const auto match =
                    std::ranges::find_if(storage.v2_artifacts, [&](const auto &candidate) {
                        if (candidate.descriptor.kind() != ArtifactKind::SymbolDefinition) {
                            return false;
                        }
                        const auto &symbol_owner =
                            std::get<LibraryAttachmentRef>(candidate.descriptor.id().owner());
                        return symbol_owner.library_namespace == owner.library_namespace &&
                               symbol_owner.library_version == owner.library_version &&
                               symbol_owner.library_bundle_digest == owner.library_bundle_digest &&
                               symbol_owner.key ==
                                   "symbol:" + symbol.name() + "@" + symbol.variant();
                    });
                require(match != storage.v2_artifacts.end(),
                        ProjectBundleOpenErrorCode::OwnershipViolation,
                        "component default symbol is not vendored");
                expected.insert(detail::project_bundle_v2_artifact_key(match->descriptor.id()));
            }
            require_exact_dependencies(descriptor_value, expected, "component definition");
            break;
        }
        case ArtifactKind::PartDefinition: {
            const auto key = detail::project_bundle_v2_artifact_key(descriptor_value.id());
            const auto &part = *decoded.parts.at(key);
            const auto &owner = std::get<LibraryPartRef>(descriptor_value.id().owner());
            auto expected = std::set<std::string>{};
            for (const auto &candidate : storage.v2_artifacts) {
                if (candidate.descriptor.kind() == ArtifactKind::ComponentDefinition) {
                    const auto &component_owner =
                        std::get<LibraryComponentRef>(candidate.descriptor.id().owner());
                    if (component_owner.library_namespace == owner.library_namespace() &&
                        component_owner.library_version == owner.library_version() &&
                        component_owner.library_bundle_digest == owner.library_digest() &&
                        component_owner.component_digest == part.implemented_component()) {
                        expected.insert(
                            detail::project_bundle_v2_artifact_key(candidate.descriptor.id()));
                    }
                }
            }
            for (const auto &asset : part_asset_references(part)) {
                for (const auto &candidate : storage.v2_artifacts) {
                    if (asset.kind() == PartAssetKind::Schematic &&
                        candidate.descriptor.kind() == ArtifactKind::SymbolDefinition) {
                        const auto &asset_owner =
                            std::get<LibraryAttachmentRef>(candidate.descriptor.id().owner());
                        if (asset_owner.key == asset.key() &&
                            asset_owner.content_digest == asset.digest() &&
                            asset_owner.library_namespace == owner.library_namespace() &&
                            asset_owner.library_version == owner.library_version() &&
                            asset_owner.library_bundle_digest == owner.library_digest()) {
                            expected.insert(
                                detail::project_bundle_v2_artifact_key(candidate.descriptor.id()));
                        }
                    } else if (asset.kind() == PartAssetKind::Footprint &&
                               candidate.descriptor.kind() == ArtifactKind::FootprintDefinition) {
                        const auto &asset_owner =
                            std::get<LibraryAttachmentRef>(candidate.descriptor.id().owner());
                        if (asset_owner.key == asset.key() &&
                            asset_owner.content_digest == asset.digest() &&
                            asset_owner.library_namespace == owner.library_namespace() &&
                            asset_owner.library_version == owner.library_version() &&
                            asset_owner.library_bundle_digest == owner.library_digest()) {
                            const auto candidate_key =
                                detail::project_bundle_v2_artifact_key(candidate.descriptor.id());
                            const auto footprint = decoded.footprints.find(candidate_key);
                            require(footprint != decoded.footprints.end() &&
                                        footprint->second.ref() ==
                                            part.orderable_part().footprint().footprint(),
                                    ProjectBundleOpenErrorCode::OwnershipViolation,
                                    "footprint payload reference disagrees with its selected part");
                            expected.insert(candidate_key);
                        }
                    } else if (asset.kind() == PartAssetKind::Model3D &&
                               asset.key().starts_with("model:glb/") &&
                               candidate.descriptor.kind() == ArtifactKind::GlbAsset) {
                        const auto &asset_owner =
                            std::get<LibraryAssetRef>(candidate.descriptor.id().owner());
                        if (asset_owner.content_digest == asset.digest() &&
                            asset_owner.library_namespace == owner.library_namespace() &&
                            asset_owner.library_version == owner.library_version() &&
                            asset_owner.library_bundle_digest == owner.library_digest()) {
                            expected.insert(
                                detail::project_bundle_v2_artifact_key(candidate.descriptor.id()));
                        }
                    }
                }
            }
            require_exact_dependencies(descriptor_value, expected, "part definition");
            break;
        }
        case ArtifactKind::LogicalModel: {
            const auto &owner = std::get<LogicalArtifactIdentity>(descriptor_value.id().owner());
            const auto circuit = circuit_for_design(storage, owner.design);
            auto expected = std::set<std::string>{};
            for (auto component_index = std::size_t{0};
                 component_index < storage.v2_circuits[circuit].model->all<ComponentId>().size();
                 ++component_index) {
                const auto component = ComponentId{component_index};
                const auto &definition =
                    storage.v2_circuits[circuit].model->get(component).definition();
                const auto &component_definition =
                    storage.v2_circuits[circuit].model->get(definition);
                if (component_definition.source().has_value()) {
                    auto match = std::optional<std::string>{};
                    for (const auto &dependency : descriptor_value.dependencies()) {
                        if (dependency.artifact().kind() != ArtifactKind::ComponentDefinition) {
                            continue;
                        }
                        const auto &candidate_owner =
                            std::get<LibraryComponentRef>(dependency.artifact().owner());
                        if (candidate_owner.component_key !=
                                component_definition.contract().key() ||
                            candidate_owner.component_digest !=
                                component_definition.content_identity()) {
                            continue;
                        }
                        require(!match.has_value(), ProjectBundleOpenErrorCode::OwnershipViolation,
                                "logical model has ambiguous imported component dependencies");
                        match = detail::project_bundle_v2_artifact_key(dependency.artifact());
                    }
                    require(match.has_value(), ProjectBundleOpenErrorCode::OwnershipViolation,
                            "logical model imported component is not vendored");
                    expected.insert(*match);
                }
                const auto &selected = queries::selected_library_part_ref(
                    *storage.v2_circuits[circuit].model, component);
                if (!selected.has_value()) {
                    continue;
                }
                expected.insert(detail::project_bundle_v2_artifact_key(
                    ArtifactId{ArtifactKind::PartDefinition, *selected}));
            }
            require_exact_dependencies(descriptor_value, expected, "logical model");
            break;
        }
        default:
            break;
        }
    }

    verify_library_reachability(storage, index);

    for (const auto &board : storage.v2_boards) {
        const auto board_key = detail::project_bundle_v2_artifact_key(
            storage.v2_artifacts[board.artifact].descriptor.id());
        auto compiled_index = std::optional<std::size_t>{};
        for (auto candidate_index = std::size_t{0};
             candidate_index < storage.v2_compiled_boards.size(); ++candidate_index) {
            const auto &candidate = storage.v2_compiled_boards[candidate_index];
            if (!dependency_keys(storage.v2_artifacts[candidate.artifact].descriptor)
                     .contains(board_key)) {
                continue;
            }
            require(!compiled_index.has_value(), ProjectBundleOpenErrorCode::OwnershipViolation,
                    "named Board has more than one CompiledBoard dependency owner");
            compiled_index = candidate_index;
        }
        require(compiled_index.has_value(), ProjectBundleOpenErrorCode::OwnershipViolation,
                "named Board has no CompiledBoard dependency owner");
        const auto scenes =
            std::ranges::count_if(storage.v2_board_scenes, [&](const auto &candidate) {
                return candidate.compiled_board == *compiled_index;
            });
        require(scenes == 1U, ProjectBundleOpenErrorCode::OwnershipViolation,
                "each named Board must have exactly one matching CompiledBoard and BoardScene");
    }
    require(storage.v2_compiled_boards.size() == storage.v2_boards.size() &&
                storage.v2_board_scenes.size() == storage.v2_boards.size(),
            ProjectBundleOpenErrorCode::OwnershipViolation,
            "CompiledBoard or BoardScene graph contains an orphan");

    for (const auto &compiled : storage.v2_compiled_boards) {
        const auto compiled_dependencies =
            dependency_keys(storage.v2_artifacts[compiled.artifact].descriptor);
        const auto board = std::ranges::find_if(storage.v2_boards, [&](const auto &candidate) {
            return compiled_dependencies.contains(detail::project_bundle_v2_artifact_key(
                storage.v2_artifacts[candidate.artifact].descriptor.id()));
        });
        require(board != storage.v2_boards.end(), ProjectBundleOpenErrorCode::OwnershipViolation,
                "CompiledBoard has no matching named authoring Board");
        require(board->board == compiled.model->identity().board(),
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "CompiledBoard name disagrees with its exact authoring Board dependency");
        const auto &circuit = *storage.v2_circuits[board->circuit].model;
        require(compiled_board_logical_dependency_digest(circuit) ==
                    compiled.model->provenance().logical_dependency_digest(),
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "CompiledBoard logical provenance disagrees with its reopened Circuit");
        const auto board_snapshot =
            nlohmann::json::parse(storage.v2_artifacts[board->artifact].bytes).dump();
        require(sha256_content_hash(board_snapshot) ==
                    compiled.model->provenance().physical_snapshot_digest(),
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "CompiledBoard physical provenance disagrees with its authoring Board");

        auto expected = std::set<std::string>{
            detail::project_bundle_v2_artifact_key(
                storage.v2_artifacts[board->artifact].descriptor.id()),
            detail::project_bundle_v2_artifact_key(
                storage.v2_artifacts[storage.v2_circuits[board->circuit].artifact]
                    .descriptor.id())};
        for (const auto &resolved : compiled.model->parts()) {
            const auto part_id = ArtifactId{ArtifactKind::PartDefinition, resolved.reference()};
            const auto part_key = detail::project_bundle_v2_artifact_key(part_id);
            const auto part_artifact = index.find(part_key);
            require(part_artifact != index.end(), ProjectBundleOpenErrorCode::OwnershipViolation,
                    "CompiledBoard selected part is not vendored");
            expected.insert(part_key);
            const auto &part = *decoded.parts.at(part_key);
            const auto &part_owner = resolved.reference();
            for (const auto &candidate : storage.v2_artifacts) {
                if (candidate.descriptor.kind() == ArtifactKind::ComponentDefinition) {
                    const auto &owner =
                        std::get<LibraryComponentRef>(candidate.descriptor.id().owner());
                    if (owner.library_namespace == part_owner.library_namespace() &&
                        owner.library_version == part_owner.library_version() &&
                        owner.library_bundle_digest == part_owner.library_digest() &&
                        owner.component_digest == part.implemented_component()) {
                        expected.insert(
                            detail::project_bundle_v2_artifact_key(candidate.descriptor.id()));
                    }
                }
            }
            for (const auto &asset : part_asset_references(part)) {
                const auto matches_origin = [&](const auto &owner) {
                    return owner.library_namespace == part_owner.library_namespace() &&
                           owner.library_version == part_owner.library_version() &&
                           owner.library_bundle_digest == part_owner.library_digest();
                };
                if (asset.kind() == PartAssetKind::Footprint) {
                    const auto match =
                        std::ranges::find_if(storage.v2_artifacts, [&](const auto &candidate) {
                            if (candidate.descriptor.kind() != ArtifactKind::FootprintDefinition) {
                                return false;
                            }
                            const auto &owner =
                                std::get<LibraryAttachmentRef>(candidate.descriptor.id().owner());
                            return matches_origin(owner) && owner.key == asset.key() &&
                                   owner.content_digest == asset.digest();
                        });
                    require(match != storage.v2_artifacts.end(),
                            ProjectBundleOpenErrorCode::OwnershipViolation,
                            "CompiledBoard footprint closure is not vendored");
                    expected.insert(detail::project_bundle_v2_artifact_key(match->descriptor.id()));
                } else if (asset.kind() == PartAssetKind::Model3D &&
                           asset.key().starts_with("model:glb/") &&
                           compiled.model->capabilities().has(BoardAssetCapability::Models3D)) {
                    const auto match =
                        std::ranges::find_if(storage.v2_artifacts, [&](const auto &candidate) {
                            if (candidate.descriptor.kind() != ArtifactKind::GlbAsset) {
                                return false;
                            }
                            const auto &owner =
                                std::get<LibraryAssetRef>(candidate.descriptor.id().owner());
                            return matches_origin(owner) && owner.content_digest == asset.digest();
                        });
                    require(match != storage.v2_artifacts.end(),
                            ProjectBundleOpenErrorCode::OwnershipViolation,
                            "CompiledBoard consumed GLB closure is not vendored");
                    expected.insert(detail::project_bundle_v2_artifact_key(match->descriptor.id()));
                }
            }
        }
        require_exact_dependencies(storage.v2_artifacts[compiled.artifact].descriptor, expected,
                                   "CompiledBoard");
    }

    for (const auto &scene : storage.v2_board_scenes) {
        const auto &compiled = *storage.v2_compiled_boards[scene.compiled_board].model;
        auto expected = std::set<std::string>{detail::project_bundle_v2_artifact_key(
            storage.v2_artifacts[storage.v2_compiled_boards[scene.compiled_board].artifact]
                .descriptor.id())};
        auto expected_model_digests = std::set<std::string>{};
        if (compiled.capabilities().has(BoardAssetCapability::Models3D)) {
            for (const auto &resolved : compiled.parts()) {
                const auto &model = resolved.physical_part().model_3d();
                if (!model.has_value() || model->format() != "glb") {
                    continue;
                }
                require(resolved.model_3d_bytes().has_value(),
                        ProjectBundleOpenErrorCode::OwnershipViolation,
                        "CompiledBoard GLB closure is missing persisted bytes");
                const auto digest = sha256_content_hash(*resolved.model_3d_bytes());
                expected_model_digests.insert(digest.value());
                const auto &part = resolved.reference();
                const auto match =
                    std::ranges::find_if(storage.v2_artifacts, [&](const auto &candidate) {
                        if (candidate.descriptor.kind() != ArtifactKind::GlbAsset) {
                            return false;
                        }
                        const auto &owner =
                            std::get<LibraryAssetRef>(candidate.descriptor.id().owner());
                        return owner.library_namespace == part.library_namespace() &&
                               owner.library_version == part.library_version() &&
                               owner.library_bundle_digest == part.library_digest() &&
                               owner.content_digest == digest;
                    });
                require(match != storage.v2_artifacts.end(),
                        ProjectBundleOpenErrorCode::OwnershipViolation,
                        "BoardScene GLB closure contains a foreign or absent asset");
                expected.insert(detail::project_bundle_v2_artifact_key(match->descriptor.id()));
            }
        }
        auto actual_model_digests = std::set<std::string>{};
        for (const auto &model : scene.model->models()) {
            actual_model_digests.insert(model.reference().digest().value());
        }
        require(actual_model_digests == expected_model_digests,
                ProjectBundleOpenErrorCode::OwnershipViolation,
                "BoardScene GLB closure is stale or belongs to another Board");
        require_exact_dependencies(storage.v2_artifacts[scene.artifact].descriptor, expected,
                                   "BoardScene");
    }
}

void verify_exports(ProjectBundleStorage &storage, const LibraryDecoded &library,
                    const ExportSelection &selection) {
    auto expected_outputs = std::set<std::string>{};
    for (const auto &request : selection) {
        const auto digest =
            sha256_content_hash(detail::project_bundle_v2_export_request_json(request));
        auto output_kind = ArtifactKind::SchematicSvg;
        auto output_key = ExportOutputKey{PrimaryExportOutputKey::Primary};
        auto expected_bytes = std::optional<std::string>{};
        switch (request.kind) {
        case ExportKind::SchematicSvg: {
            const auto &target = std::get<ModelExportTarget>(request.target);
            output_kind = ArtifactKind::SchematicSvg;
            const auto schematic =
                std::ranges::find_if(storage.v2_schematics, [&](const auto &candidate) {
                    return storage.v2_artifacts[candidate.artifact].descriptor.id() ==
                           target.model.artifact();
                });
            require(schematic != storage.v2_schematics.end(),
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "Schematic SVG target is not a loaded Schematic");
            require(storage.v2_artifacts[schematic->artifact].descriptor.content_digest() ==
                        target.model.content_digest(),
                    ProjectBundleOpenErrorCode::DigestMismatch,
                    "Schematic SVG target digest does not match its loaded Schematic");
            expected_bytes = write_schematic_svg(*schematic->model);
            break;
        }
        case ExportKind::BoardSvg: {
            const auto &target = std::get<ModelExportTarget>(request.target);
            output_kind = ArtifactKind::BoardSvg;
            const auto compiled =
                std::ranges::find_if(storage.v2_compiled_boards, [&](const auto &candidate) {
                    return storage.v2_artifacts[candidate.artifact].descriptor.id() ==
                           target.model.artifact();
                });
            require(compiled != storage.v2_compiled_boards.end(),
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "Board SVG target is not a loaded CompiledBoard");
            require(storage.v2_artifacts[compiled->artifact].descriptor.content_digest() ==
                        target.model.content_digest(),
                    ProjectBundleOpenErrorCode::DigestMismatch,
                    "Board SVG target digest does not match its loaded CompiledBoard");
            expected_bytes =
                write_pcb_placement_svg(compiled->model->board(), compiled->model->footprints());
            break;
        }
        case ExportKind::BoardLayerImage: {
            const auto &target = std::get<BoardLayerExportTarget>(request.target);
            output_kind = ArtifactKind::BoardLayerImage;
            output_key = target.layer;
            const auto compiled =
                std::ranges::find_if(storage.v2_compiled_boards, [&](const auto &candidate) {
                    return storage.v2_artifacts[candidate.artifact].descriptor.id() ==
                           target.compiled_board.artifact();
                });
            require(compiled != storage.v2_compiled_boards.end(),
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "Board layer export target is not a loaded CompiledBoard");
            require(storage.v2_artifacts[compiled->artifact].descriptor.content_digest() ==
                        target.compiled_board.content_digest(),
                    ProjectBundleOpenErrorCode::DigestMismatch,
                    "Board layer export target digest does not match its loaded CompiledBoard");
            auto layer = std::optional<BoardLayerId>{};
            for (auto index = std::size_t{0};
                 index < compiled->model->board().all<BoardLayerId>().size(); ++index) {
                if (compiled->model->board().get(BoardLayerId{index}).name() ==
                    target.layer.value()) {
                    require(!layer.has_value(), ProjectBundleOpenErrorCode::OwnershipViolation,
                            "Board layer export target is ambiguous");
                    layer = BoardLayerId{index};
                }
            }
            require(layer.has_value(), ProjectBundleOpenErrorCode::OwnershipViolation,
                    "Board layer export target is absent from its CompiledBoard");
            expected_bytes =
                write_pcb_placement_svg(compiled->model->board(), compiled->model->footprints(),
                                        PcbPlacementSvgOptions{.layer_filter = layer});
            break;
        }
        case ExportKind::Bom: {
            const auto &target = std::get<ModelExportTarget>(request.target);
            output_kind = ArtifactKind::Bom;
            const auto circuit =
                std::ranges::find_if(storage.v2_circuits, [&](const auto &candidate) {
                    return storage.v2_artifacts[candidate.artifact].descriptor.id() ==
                           target.model.artifact();
                });
            require(circuit != storage.v2_circuits.end(),
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "BOM target is not a loaded logical Circuit");
            require(storage.v2_artifacts[circuit->artifact].descriptor.content_digest() ==
                        target.model.content_digest(),
                    ProjectBundleOpenErrorCode::DigestMismatch,
                    "BOM target digest does not match its loaded logical Circuit");
            expected_bytes = write_bom_json(project_bom(*circuit->model));
            break;
        }
        case ExportKind::Cpl: {
            const auto &target = std::get<ModelExportTarget>(request.target);
            output_kind = ArtifactKind::Cpl;
            const auto compiled =
                std::ranges::find_if(storage.v2_compiled_boards, [&](const auto &candidate) {
                    return storage.v2_artifacts[candidate.artifact].descriptor.id() ==
                           target.model.artifact();
                });
            require(compiled != storage.v2_compiled_boards.end(),
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "CPL target is not a loaded CompiledBoard");
            require(storage.v2_artifacts[compiled->artifact].descriptor.content_digest() ==
                        target.model.content_digest(),
                    ProjectBundleOpenErrorCode::DigestMismatch,
                    "CPL target digest does not match its loaded CompiledBoard");
            expected_bytes = write_cpl_json(project_cpl(*compiled->model).cpl());
            break;
        }
        case ExportKind::Step: {
            const auto &target = std::get<LibraryAssetExportTarget>(request.target);
            output_kind = ArtifactKind::StepAsset;
            output_key = target.asset;
            const auto selected_part =
                std::ranges::find_if(storage.v2_artifacts, [&](const auto &candidate) {
                    return candidate.descriptor.id() == target.selected_part.artifact();
                });
            require(selected_part != storage.v2_artifacts.end(),
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "STEP export selected part is absent");
            require(selected_part->descriptor.content_digest() ==
                        target.selected_part.content_digest(),
                    ProjectBundleOpenErrorCode::DigestMismatch,
                    "STEP export selected-part digest does not match its loaded definition");
            const auto part = library.parts.find(
                detail::project_bundle_v2_artifact_key(selected_part->descriptor.id()));
            require(part != library.parts.end(), ProjectBundleOpenErrorCode::OwnershipViolation,
                    "STEP export selected part was not decoded");
            const auto &model = part->second->orderable_part().model_3d();
            const auto &part_owner =
                std::get<LibraryPartRef>(selected_part->descriptor.id().owner());
            require(model.has_value() && model->format() == "step" &&
                        model->hash() == target.asset.content_digest &&
                        part_owner.library_namespace() == target.asset.library_namespace &&
                        part_owner.library_version() == target.asset.library_version &&
                        part_owner.library_digest() == target.asset.library_bundle_digest,
                    ProjectBundleOpenErrorCode::OwnershipViolation,
                    "STEP export asset is not the exact selected-part model closure");
            break;
        }
        case ExportKind::KicadPcb:
        case ExportKind::Fabrication:
        case ExportKind::WholeBoardGlb:
            fail(ProjectBundleOpenErrorCode::UnsupportedFormat,
                 "unsupported v2 producer export reached output verification");
        }
        const auto expected_id =
            ArtifactId{output_kind, ExportArtifactIdentity{digest, std::move(output_key)}};
        const auto expected_key = detail::project_bundle_v2_artifact_key(expected_id);
        const auto match = std::ranges::find_if(storage.v2_artifacts, [&](const auto &artifact) {
            return detail::project_bundle_v2_artifact_key(artifact.descriptor.id()) == expected_key;
        });
        require(match != storage.v2_artifacts.end(), ProjectBundleOpenErrorCode::OwnershipViolation,
                "selected export has no exact output artifact");
        if (expected_bytes.has_value()) {
            require(match->bytes == *expected_bytes, ProjectBundleOpenErrorCode::ModelDecodeFailure,
                    "selected export payload disagrees with its exact reopened target");
        }
        if (request.kind == ExportKind::Step) {
            const auto &target = std::get<LibraryAssetExportTarget>(request.target);
            require(match->descriptor.content_digest() == target.asset.content_digest,
                    ProjectBundleOpenErrorCode::DigestMismatch,
                    "STEP output bytes do not match the exact selected asset");
        }
        expected_outputs.insert(expected_key);
        auto expected_dependencies = std::set<std::string>{};
        std::visit(
            [&](const auto &target) {
                using T = std::decay_t<decltype(target)>;
                if constexpr (std::same_as<T, ModelExportTarget>) {
                    expected_dependencies.insert(
                        detail::project_bundle_v2_artifact_key(target.model.artifact()));
                } else if constexpr (std::same_as<T, BoardLayerExportTarget>) {
                    expected_dependencies.insert(
                        detail::project_bundle_v2_artifact_key(target.compiled_board.artifact()));
                } else {
                    expected_dependencies.insert(
                        detail::project_bundle_v2_artifact_key(target.selected_part.artifact()));
                }
            },
            request.target);
        require_exact_dependencies(match->descriptor, expected_dependencies, "selected export");
        storage.v2_selected_exports.push_back(
            static_cast<std::size_t>(match - storage.v2_artifacts.begin()));
    }
    auto actual_outputs = std::set<std::string>{};
    for (const auto &artifact : storage.v2_artifacts) {
        if (is_export_artifact(artifact.descriptor)) {
            actual_outputs.insert(detail::project_bundle_v2_artifact_key(artifact.descriptor.id()));
        }
    }
    require(expected_outputs == actual_outputs, ProjectBundleOpenErrorCode::OwnershipViolation,
            "export selection and output artifacts do not form an exact bijection");
}

} // namespace volt::io::v2_open
