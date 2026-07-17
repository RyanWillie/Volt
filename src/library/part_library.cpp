#include <volt/library/part_library.hpp>

#include <algorithm>
#include <sstream>
#include <string_view>
#include <utility>

#include <volt/core/errors.hpp>

namespace volt {

namespace {

void append_string(std::ostringstream &out, std::string_view value) {
    out << value.size() << ':' << value << '\n';
}

[[nodiscard]] PartKey part_key(const PartDefinition &part) {
    return PartKey{part.identity().name()};
}

[[nodiscard]] ContentHash library_digest(const PartLibraryIdentity &identity,
                                         std::span<const ComponentDefinition> components,
                                         std::span<const PartDefinition> parts) {
    auto out = std::ostringstream{};
    append_string(out, "volt.part-library");
    append_string(out, std::to_string(static_cast<std::uint32_t>(identity.schema_version())));
    append_string(out, identity.namespace_name());
    append_string(out, identity.version());
    append_string(out, std::to_string(components.size()));
    for (const auto &component : components) {
        append_string(out, component.contract().key().value());
        append_string(out, component.content_identity().value());
    }
    append_string(out, std::to_string(parts.size()));
    for (const auto &part : parts) {
        append_string(out, part.identity().name());
        append_string(out, part.content_identity().value());
    }
    return sha256_content_hash(out.str());
}

void validate_assets(std::span<const PartDefinition> parts,
                     const PartAssetResolver &asset_resolver) {
    for (const auto &part : parts) {
        for (const auto &reference : part_asset_references(part)) {
            const auto bytes = asset_resolver.resolve(reference);
            if (!bytes.has_value()) {
                throw KernelRangeError{ErrorCode::UnknownEntity,
                                       "Part library asset is unavailable: " + reference.key()};
            }
            if (sha256_content_hash(*bytes) != reference.digest()) {
                throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                       "Part library asset digest does not match resolved bytes: " +
                                           reference.key()};
            }
        }
    }
}

} // namespace

PartKey::PartKey(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Part key must not be empty"};
    }
}

PartLibraryIdentity::PartLibraryIdentity(std::string namespace_name, std::string version,
                                         PartLibrarySchemaVersion schema_version)
    : namespace_{std::move(namespace_name)}, version_{std::move(version)},
      schema_version_{schema_version} {
    if (namespace_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Part library namespace must not be empty"};
    }
    if (version_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Part library version must not be empty"};
    }
    if (schema_version_ != PartLibrarySchemaVersion::V1) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Part library schema version is unsupported"};
    }
}

LibraryPartRef::LibraryPartRef(std::string library_namespace, std::string library_version,
                               PartKey part_key, ContentHash library_digest,
                               ContentHash part_digest)
    : library_namespace_{std::move(library_namespace)},
      library_version_{std::move(library_version)}, part_key_{std::move(part_key)},
      library_digest_{std::move(library_digest)}, part_digest_{std::move(part_digest)} {
    if (library_namespace_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "LibraryPartRef namespace must not be empty"};
    }
    if (library_version_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "LibraryPartRef version must not be empty"};
    }
}

PartAssetReference::PartAssetReference(PartAssetKind kind, std::string key, ContentHash digest)
    : kind_{kind}, key_{std::move(key)}, digest_{std::move(digest)} {
    switch (kind_) {
    case PartAssetKind::Schematic:
    case PartAssetKind::Footprint:
    case PartAssetKind::Model3D:
        break;
    default:
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Part asset kind is unsupported"};
    }
    if (key_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Part asset resolver key must not be empty"};
    }
}

std::vector<PartAssetReference> part_asset_references(const PartDefinition &part) {
    auto result = std::vector<PartAssetReference>{};
    result.reserve(part.schematic_assets().size() +
                   (part.orderable_part().model_3d().has_value() ? 2U : 1U));
    for (const auto &asset : part.schematic_assets()) {
        result.emplace_back(PartAssetKind::Schematic,
                            "symbol:" + asset.name() + "@" + asset.variant(), asset.hash());
    }
    const auto &footprint = part.orderable_part().footprint();
    result.emplace_back(PartAssetKind::Footprint,
                        "footprint:" + footprint.footprint().library() + "/" +
                            footprint.footprint().name(),
                        footprint.hash());
    if (part.orderable_part().model_3d().has_value()) {
        const auto &model = *part.orderable_part().model_3d();
        result.emplace_back(PartAssetKind::Model3D,
                            "model:" + model.format() + "/" + model.file_name(), model.hash());
    }
    return result;
}

PartLibraryBuilder::PartLibraryBuilder(PartLibraryIdentity identity)
    : identity_{std::move(identity)} {}

PartLibraryBuilder &PartLibraryBuilder::add_component(ComponentDefinition component) {
    const auto duplicate_key =
        std::ranges::find(components_, component.contract().key(),
                          [](const auto &candidate) { return candidate.contract().key(); });
    if (duplicate_key != components_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Part library component key already exists"};
    }
    const auto duplicate_digest = std::ranges::find(components_, component.content_identity(),
                                                    &ComponentDefinition::content_identity);
    if (duplicate_digest != components_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Part library component digest already exists"};
    }
    components_.push_back(std::move(component));
    return *this;
}

PartLibraryBuilder &PartLibraryBuilder::add_part(PartDefinition part) {
    if (part.identity().namespace_name() != identity_.namespace_name()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Exact part namespace does not match its part library"};
    }
    const auto component = std::ranges::find(components_, part.implemented_component(),
                                             &ComponentDefinition::content_identity);
    if (component == components_.end()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Exact part implements a component outside its part library"};
    }
    const auto key = part_key(part);
    const auto duplicate_key = std::ranges::find(
        parts_, key.value(), [](const auto &candidate) { return candidate.identity().name(); });
    if (duplicate_key != parts_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName, "Part library part key already exists"};
    }
    const auto duplicate_digest =
        std::ranges::find(parts_, part.content_identity(), &PartDefinition::content_identity);
    if (duplicate_digest != parts_.end()) {
        throw KernelArgumentError{ErrorCode::DuplicateName,
                                  "Part library part digest already exists"};
    }
    parts_.push_back(std::move(part));
    return *this;
}

PartLibrary PartLibraryBuilder::build(const PartAssetResolver &asset_resolver) const {
    return PartLibrary{*this, asset_resolver};
}

PartLibrary::PartLibrary(const PartLibraryBuilder &builder, const PartAssetResolver &asset_resolver)
    : identity_{builder.identity()},
      components_{builder.components().begin(), builder.components().end()},
      parts_{builder.parts().begin(), builder.parts().end()}, digest_{sha256_content_hash("")} {
    std::ranges::sort(components_, {},
                      [](const auto &component) { return component.contract().key(); });
    std::ranges::sort(parts_, {}, [](const auto &part) { return part_key(part); });
    validate_assets(parts_, asset_resolver);
    digest_ = library_digest(identity_, components_, parts_);
}

std::optional<std::reference_wrapper<const ComponentDefinition>>
PartLibrary::component(const ComponentKey &key) const & noexcept {
    const auto match = std::ranges::find(
        components_, key, [](const auto &candidate) { return candidate.contract().key(); });
    if (match == components_.end()) {
        return std::nullopt;
    }
    return std::cref(*match);
}

std::optional<std::reference_wrapper<const PartDefinition>>
PartLibrary::part(const PartKey &key) const & noexcept {
    const auto match = std::ranges::find(
        parts_, key.value(), [](const auto &candidate) { return candidate.identity().name(); });
    if (match == parts_.end()) {
        return std::nullopt;
    }
    return std::cref(*match);
}

std::vector<std::reference_wrapper<const PartDefinition>>
PartLibrary::find(const PartQuery &query) const & {
    const auto matched_component = component(query.component);
    if (!matched_component.has_value()) {
        return {};
    }
    auto result = std::vector<std::reference_wrapper<const PartDefinition>>{};
    for (const auto &candidate : parts_) {
        if (candidate.implemented_component() != matched_component->get().content_identity()) {
            continue;
        }
        if (query.manufacturer_part.has_value() &&
            candidate.orderable_part().manufacturer_part() != *query.manufacturer_part) {
            continue;
        }
        if (query.package.has_value() && candidate.orderable_part().package() != *query.package) {
            continue;
        }
        result.push_back(std::cref(candidate));
    }
    return result;
}

LibraryPartRef PartLibrary::require(const PartKey &key) const {
    const auto candidate = part(key);
    if (!candidate.has_value()) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Part key does not exist in this library snapshot"};
    }
    return LibraryPartRef{identity_.namespace_name(), identity_.version(), key, digest_,
                          candidate->get().content_identity()};
}

const PartDefinition &PartLibrary::resolve(const LibraryPartRef &reference) const & {
    if (reference.library_namespace() != identity_.namespace_name() ||
        reference.library_version() != identity_.version()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "LibraryPartRef identifies a different library release"};
    }
    if (reference.library_digest() != digest_) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "LibraryPartRef library digest does not match this snapshot"};
    }
    const auto candidate = part(reference.part_key());
    if (!candidate.has_value()) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "LibraryPartRef part key does not exist in this snapshot"};
    }
    if (reference.part_digest() != candidate->get().content_identity()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "LibraryPartRef part digest does not match its exact key"};
    }
    const auto implemented_component =
        std::ranges::find(components_, candidate->get().implemented_component(),
                          &ComponentDefinition::content_identity);
    if (implemented_component == components_.end()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Resolved exact part has no matching component contract"};
    }
    return candidate->get();
}

} // namespace volt
