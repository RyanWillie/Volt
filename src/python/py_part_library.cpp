#include "py_part_library.hpp"

#include "binding_diagnostic_conversions.hpp"
#include "binding_part_definition_conversions.hpp"

#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

#include <volt/core/errors.hpp>
#include <volt/io/parts/part_definition_writer.hpp>

namespace volt::python {
namespace {

[[nodiscard]] std::string asset_key(volt::PartAssetKind kind, const std::string &key) {
    return std::to_string(static_cast<int>(kind)) + ":" + key;
}

[[nodiscard]] volt::PartAssetKind asset_kind_from_string(const std::string &kind) {
    if (kind == "schematic") {
        return volt::PartAssetKind::Schematic;
    }
    if (kind == "footprint") {
        return volt::PartAssetKind::Footprint;
    }
    if (kind == "model_3d") {
        return volt::PartAssetKind::Model3D;
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Part asset kind is unsupported"};
}

class PayloadAssetResolver final : public volt::PartAssetResolver {
  public:
    void add(const py::dict &asset) {
        const auto kind = asset_kind_from_string(required_string_field(asset, "kind"));
        const auto key = required_string_field(asset, "key");
        const auto bytes = static_cast<std::string>(py::cast<py::bytes>(asset["bytes"]));
        const auto [match, inserted] = assets_.try_emplace(asset_key(kind, key), bytes);
        if (!inserted && match->second != bytes) {
            throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                         "Part asset key resolves to conflicting bytes"};
        }
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &reference) const override {
        const auto match = assets_.find(asset_key(reference.kind(), reference.key()));
        if (match == assets_.end()) {
            return std::nullopt;
        }
        return match->second;
    }

  private:
    std::map<std::string, std::string> assets_;
};

struct BuiltLibrary {
    volt::PartLibrary library;
    std::map<std::string, volt::ComponentSpec> component_specs;
};

[[nodiscard]] BuiltLibrary build_library(const std::string &namespace_name,
                                         const std::string &version, const py::list &parts) {
    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{namespace_name, version, volt::PartLibrarySchemaVersion::V1}};
    auto resolver = PayloadAssetResolver{};
    auto specs = std::map<std::string, volt::ComponentSpec>{};
    auto component_digests = std::map<std::string, volt::ContentHash>{};

    for (const auto item : parts) {
        const auto payload = py::cast<py::dict>(item);
        auto lowered = lower_part_definition_from_dict(payload);
        const auto component_key = lowered.component.contract().key().value();
        const auto digest = lowered.component.content_identity();
        const auto existing = component_digests.find(component_key);
        if (existing == component_digests.end()) {
            builder.add_component(lowered.component);
            component_digests.emplace(component_key, digest);
        } else if (existing->second != digest) {
            throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                         "ComponentKey resolves to conflicting component content"};
        }
        const auto part_key = lowered.part.identity().name();
        specs.emplace(part_key, std::move(lowered.component_spec));
        builder.add_part(std::move(lowered.part));
        for (const auto asset_item : required_list_field(payload, "assets")) {
            resolver.add(py::cast<py::dict>(asset_item));
        }
    }
    return BuiltLibrary{builder.build(resolver), std::move(specs)};
}

} // namespace

PyPartLibrary::PyPartLibrary(std::string namespace_name, std::string version,
                             const py::list &parts) {
    try {
        auto built = build_library(namespace_name, version, parts);
        state_ =
            std::make_shared<State>(std::move(built.library), std::move(built.component_specs));
    } catch (const volt::KernelError &) {
        throw;
    } catch (const std::invalid_argument &error) {
        throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument, error.what()};
    } catch (const std::out_of_range &error) {
        throw volt::KernelRangeError{volt::ErrorCode::UnknownEntity, error.what()};
    }
}

const volt::PartLibrary &PyPartLibrary::library() const noexcept { return state_->library; }

const volt::ComponentSpec &PyPartLibrary::component_spec(const std::string &part_key) const {
    const auto match = state_->component_specs.find(part_key);
    if (match == state_->component_specs.end()) {
        throw volt::KernelRangeError{volt::ErrorCode::UnknownEntity,
                                     "Part library has no requested component lowering"};
    }
    return match->second;
}

py::dict library_part_reference_to_dict(const volt::LibraryPartRef &reference) {
    auto result = py::dict{};
    result["library_namespace"] = reference.library_namespace();
    result["library_version"] = reference.library_version();
    result["part_key"] = reference.part_key().value();
    result["library_digest"] = reference.library_digest().value();
    result["part_digest"] = reference.part_digest().value();
    return result;
}

py::dict PyPartLibrary::part_result(const std::string &part_key) const {
    const auto key = volt::PartKey{part_key};
    const auto &part = state_->library.resolve(state_->library.require(key));
    auto result = py::dict{};
    const auto bytes = volt::io::write_part_definition(part);
    result["bytes"] = py::bytes{bytes};
    result["sha256"] = volt::io::part_definition_content_hash(part).value();
    result["component_sha256"] = part.implemented_component().value();
    result["diagnostics"] = diagnostics_to_list(volt::validate_part_lineup(part));
    result["exact_reference"] = exact_reference(part_key);
    return result;
}

py::dict PyPartLibrary::exact_reference(const std::string &part_key) const {
    return library_part_reference_to_dict(state_->library.require(volt::PartKey{part_key}));
}

std::string PyPartLibrary::digest() const { return state_->library.digest().value(); }

} // namespace volt::python
