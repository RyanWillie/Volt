#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <volt/circuit/parts/part_definition.hpp>
#include <volt/circuit/parts/selected_part.hpp>
#include <volt/core/diagnostics.hpp>

namespace volt {

class Circuit;

/** Supported semantic schema for one in-memory native part-library snapshot. */
enum class PartLibrarySchemaVersion : std::uint32_t {
    V1 = 1,
};

/** Human identity and supported schema of one immutable in-memory library snapshot. */
class PartLibraryIdentity {
  public:
    /** Construct a library identity from non-empty namespace and version fields. */
    PartLibraryIdentity(std::string namespace_name, std::string version,
                        PartLibrarySchemaVersion schema_version);

    /** Return the library namespace. */
    [[nodiscard]] const std::string &namespace_name() const noexcept { return namespace_; }

    /** Return the human library release version. */
    [[nodiscard]] const std::string &version() const noexcept { return version_; }

    /** Return the supported native library semantic schema. */
    [[nodiscard]] PartLibrarySchemaVersion schema_version() const noexcept {
        return schema_version_;
    }

  private:
    std::string namespace_;
    std::string version_;
    PartLibrarySchemaVersion schema_version_;
};

/** Closed asset kinds referenced by exact P3 part definitions. */
enum class PartAssetKind {
    Schematic,
    Footprint,
    Model3D,
};

/** Typed content-addressed asset request made during native library construction. */
class PartAssetReference {
  public:
    /** Construct an explicit typed request from a non-empty stable asset key and digest. */
    PartAssetReference(PartAssetKind kind, std::string key, ContentHash digest);

    /** Return the closed asset kind. */
    [[nodiscard]] PartAssetKind kind() const noexcept { return kind_; }

    /** Return the stable resolver key. */
    [[nodiscard]] const std::string &key() const noexcept { return key_; }

    /** Return the required immutable content digest. */
    [[nodiscard]] const ContentHash &digest() const noexcept { return digest_; }

  private:
    PartAssetKind kind_;
    std::string key_;
    ContentHash digest_;
};

/** Explicit caller-supplied source of immutable asset bytes. */
class PartAssetResolver {
  public:
    /** Resolve one exact typed asset request, or return no value when it is unavailable. */
    [[nodiscard]] virtual std::optional<std::string>
    resolve(const PartAssetReference &reference) const = 0;

    /** Destroy a resolver implementation. */
    virtual ~PartAssetResolver() = default;
};

/** Return every content-addressed asset reference owned by one exact part. */
[[nodiscard]] std::vector<PartAssetReference> part_asset_references(const PartDefinition &part);

/** Typed exact filters for deterministic candidate discovery. */
struct PartQuery {
    /** Required reusable component-contract key. */
    ComponentKey component;
    /** Optional exact manufacturer and MPN filter. */
    std::optional<ManufacturerPart> manufacturer_part = std::nullopt;
    /** Optional exact package filter. */
    std::optional<PackageRef> package = std::nullopt;
};

class PartLibrary;

/** Append-only constructor for one validated immutable native part-library snapshot. */
class PartLibraryBuilder {
  public:
    /** Begin one library build with an exact human identity and supported schema. */
    explicit PartLibraryBuilder(PartLibraryIdentity identity);

    /** Append one complete immutable component definition, rejecting duplicate keys. */
    PartLibraryBuilder &add_component(ComponentDefinition component);

    /** Append one exact immutable part after its implemented component has been added. */
    PartLibraryBuilder &add_part(PartDefinition part);

    /** Build one immutable snapshot after resolving and verifying every referenced asset. */
    [[nodiscard]] PartLibrary build(const PartAssetResolver &asset_resolver) const;

    /** Return the exact identity of the pending build. */
    [[nodiscard]] const PartLibraryIdentity &identity() const & noexcept { return identity_; }

    /** Prevent borrowing the pending identity from a temporary builder. */
    [[nodiscard]] const PartLibraryIdentity &identity() const && = delete;

    /** Return complete pending component definitions without mutable access. */
    [[nodiscard]] std::span<const ComponentDefinition> components() const & noexcept {
        return components_;
    }

    /** Prevent borrowing pending component definitions from a temporary builder. */
    [[nodiscard]] std::span<const ComponentDefinition> components() const && = delete;

    /** Return complete pending exact parts without mutable access. */
    [[nodiscard]] std::span<const PartDefinition> parts() const & noexcept { return parts_; }

    /** Prevent borrowing pending exact parts from a temporary builder. */
    [[nodiscard]] std::span<const PartDefinition> parts() const && = delete;

  private:
    PartLibraryIdentity identity_;
    std::vector<ComponentDefinition> components_;
    std::vector<PartDefinition> parts_;
};

/** Immutable validated C++-owned in-memory part-library snapshot. */
class PartLibrary {
  public:
    /** Build from one structurally valid builder and one explicit asset resolver. */
    PartLibrary(const PartLibraryBuilder &builder, const PartAssetResolver &asset_resolver);

    /** Return the exact human identity and native schema. */
    [[nodiscard]] const PartLibraryIdentity &identity() const & noexcept { return identity_; }

    /** Prevent borrowing the library identity from a temporary snapshot. */
    [[nodiscard]] const PartLibraryIdentity &identity() const && = delete;

    /** Return the deterministic semantic digest of this immutable snapshot. */
    [[nodiscard]] const ContentHash &digest() const & noexcept { return digest_; }

    /** Prevent borrowing the library digest from a temporary snapshot. */
    [[nodiscard]] const ContentHash &digest() const && = delete;

    /** Return all component definitions in stable ComponentKey order. */
    [[nodiscard]] std::span<const ComponentDefinition> components() const & noexcept {
        return components_;
    }

    /** Prevent borrowing component definitions from a temporary snapshot. */
    [[nodiscard]] std::span<const ComponentDefinition> components() const && = delete;

    /** Return all exact parts in stable PartKey order. */
    [[nodiscard]] std::span<const PartDefinition> parts() const & noexcept { return parts_; }

    /** Prevent borrowing exact parts from a temporary snapshot. */
    [[nodiscard]] std::span<const PartDefinition> parts() const && = delete;

    /** Find one component by its exact typed key. */
    [[nodiscard]] std::optional<std::reference_wrapper<const ComponentDefinition>>
    component(const ComponentKey &key) const & noexcept;

    /** Prevent borrowing a component definition from a temporary snapshot. */
    [[nodiscard]] std::optional<std::reference_wrapper<const ComponentDefinition>>
    component(const ComponentKey &key) const && = delete;

    /** Find one part by its exact typed key without integrity resolution. */
    [[nodiscard]] std::optional<std::reference_wrapper<const PartDefinition>>
    part(const PartKey &key) const & noexcept;

    /** Prevent borrowing an exact part from a temporary snapshot. */
    [[nodiscard]] std::optional<std::reference_wrapper<const PartDefinition>>
    part(const PartKey &key) const && = delete;

    /** Return deterministic exact-part candidates matching all typed filters. */
    [[nodiscard]] std::vector<std::reference_wrapper<const PartDefinition>>
    find(const PartQuery &query) const &;

    /** Prevent borrowing exact candidates from a temporary snapshot. */
    [[nodiscard]] std::vector<std::reference_wrapper<const PartDefinition>>
    find(const PartQuery &query) const && = delete;

    /** Pin one existing part as a complete integrity-bearing exact reference. */
    [[nodiscard]] LibraryPartRef require(const PartKey &key) const;

    /** Resolve one exact reference or fail with a typed structural kernel error. */
    [[nodiscard]] const PartDefinition &resolve(const LibraryPartRef &reference) const &;

    /** Prevent resolving a borrowed part from a temporary snapshot. */
    [[nodiscard]] const PartDefinition &resolve(const LibraryPartRef &reference) const && = delete;

  private:
    PartLibraryIdentity identity_;
    std::vector<ComponentDefinition> components_;
    std::vector<PartDefinition> parts_;
    ContentHash digest_;
};

/** Validate selected exact-part Voltage and continuous-Current semantics against connectivity. */
[[nodiscard]] DiagnosticReport validate_selected_part_erc(const Circuit &circuit,
                                                          const PartLibrary &library);

} // namespace volt
