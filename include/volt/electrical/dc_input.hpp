#pragma once

#include <concepts>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/parts/part_definition.hpp>
#include <volt/core/content_hash.hpp>
#include <volt/core/ids.hpp>

namespace volt {

/** Exact immutable logical and selected-Part input identities for one DC request. */
class DcInputIdentity {
  public:
    /** Construct an exact identity from canonical logical and selected-Part digests. */
    DcInputIdentity(ContentHash logical, ContentHash selected_parts)
        : logical_{std::move(logical)}, selected_parts_{std::move(selected_parts)} {}

    /** Return the digest of the canonical logical Circuit. */
    [[nodiscard]] const ContentHash &logical() const noexcept { return logical_; }

    /** Return the digest of occurrence-keyed exact selected-Part references. */
    [[nodiscard]] const ContentHash &selected_parts() const noexcept { return selected_parts_; }

    /** Compare complete exact input identities. */
    [[nodiscard]] bool operator==(const DcInputIdentity &) const noexcept = default;

  private:
    ContentHash logical_;
    ContentHash selected_parts_;
};

/** Owning immutable S1 snapshot of one Circuit and its exact resolved Part inputs. */
class DcInput {
    struct Storage;

  public:
    /** Exact IO owner allowed to prepare a verified snapshot. */
    class Codec;

    /** Input-bound reference to one document-local Circuit entity. */
    template <typename Id>
        requires(std::same_as<Id, NetId> || std::same_as<Id, ComponentId>)
    class Reference {
      public:
        /** Validate and retain one entity from the supplied exact input. */
        Reference(const DcInput &input, Id id)
            : storage_{input.storage_}, input_{input.identity()}, id_{id} {
            static_cast<void>(input.circuit().get(id));
        }

        /** Return the stable document-local entity ID. */
        [[nodiscard]] Id id() const noexcept { return id_; }

        /** Return the exact input identity captured with this reference. */
        [[nodiscard]] const DcInputIdentity &input() const noexcept { return input_; }

        /** Compare exact input identity and document-local entity ID. */
        [[nodiscard]] bool operator==(const Reference &other) const noexcept {
            return input_ == other.input_ && id_ == other.id_;
        }

      private:
        std::shared_ptr<const Storage> storage_;
        DcInputIdentity input_;
        Id id_;
    };

    /** Return the immutable owned Circuit snapshot. */
    [[nodiscard]] const Circuit &circuit() const &;
    /** Prevent borrowing the Circuit from a temporary input owner. */
    [[nodiscard]] const Circuit &circuit() const && = delete;

    /** Return the exact identity of the prepared input. */
    [[nodiscard]] const DcInputIdentity &identity() const &;
    /** Prevent borrowing the identity from a temporary input owner. */
    [[nodiscard]] const DcInputIdentity &identity() const && = delete;

    /** Return the resolved exact Part, or null for unselected or unresolved occurrences. */
    [[nodiscard]] const PartDefinition *part(ComponentId occurrence) const &;
    /** Prevent borrowing a resolved Part from a temporary input owner. */
    [[nodiscard]] const PartDefinition *part(ComponentId occurrence) const && = delete;

    /** Validate and return an exact-input-bound logical net reference. */
    [[nodiscard]] Reference<NetId> net(NetId id) const { return Reference<NetId>{*this, id}; }

    /** Validate and return an exact-input-bound component occurrence reference. */
    [[nodiscard]] Reference<ComponentId> occurrence(ComponentId id) const {
        return Reference<ComponentId>{*this, id};
    }

  private:
    DcInput(Circuit circuit, DcInputIdentity identity,
            std::vector<std::optional<PartDefinition>> parts);

    std::shared_ptr<const Storage> storage_;
};

/** Exact-input-bound logical net reference. */
using DcNetRef = DcInput::Reference<NetId>;
/** Exact-input-bound component occurrence reference. */
using DcOccurrenceRef = DcInput::Reference<ComponentId>;

} // namespace volt
