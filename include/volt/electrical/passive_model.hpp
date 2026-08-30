#pragma once

#include <compare>
#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/core/content_hash.hpp>
#include <volt/core/errors.hpp>
#include <volt/core/quantities.hpp>

namespace volt {

/** Strongly typed, non-empty identity local to one passive electrical model. */
template <typename Tag> class ElectricalModelKey {
  public:
    /** Construct a portable model-local key. */
    explicit ElectricalModelKey(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Electrical-model key must not be empty"};
        }
    }

    /** Return the stable key spelling. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare keys within the same collection. */
    [[nodiscard]] bool operator==(const ElectricalModelKey &) const noexcept = default;

    /** Order keys within the same collection. */
    [[nodiscard]] std::strong_ordering
    operator<=>(const ElectricalModelKey &) const noexcept = default;

  private:
    std::string value_;
};

/** Type tag for model terminals. */
struct ModelTerminalKeyTag;
/** Type tag for private model nodes. */
struct ModelInternalNodeKeyTag;
/** Type tag for model elements. */
struct ModelElementKeyTag;

/** Stable identity of one model terminal. */
using ModelTerminalKey = ElectricalModelKey<ModelTerminalKeyTag>;
/** Stable identity of one private model node. */
using ModelInternalNodeKey = ElectricalModelKey<ModelInternalNodeKeyTag>;
/** Stable identity of one model element. */
using ModelElementKey = ElectricalModelKey<ModelElementKeyTag>;
/** Closed portable endpoint: either a contract terminal or a private node. */
using ModelEndpoint = std::variant<ModelTerminalKey, ModelInternalNodeKey>;

/** One explicit binding from a model terminal to a logical contract pin. */
class ModelTerminal {
  public:
    /** Bind one terminal key to one contract PinKey. */
    ModelTerminal(ModelTerminalKey key, PinKey pin) : key_{std::move(key)}, pin_{std::move(pin)} {}

    /** Return the model-local terminal key. */
    [[nodiscard]] const ModelTerminalKey &key() const noexcept { return key_; }

    /** Return the implemented contract pin. */
    [[nodiscard]] const PinKey &pin() const noexcept { return pin_; }

  private:
    ModelTerminalKey key_;
    PinKey pin_;
};

/** A private model node, inaccessible to Circuit connectivity. */
class ModelInternalNode {
  public:
    /** Declare one private model-local node. */
    explicit ModelInternalNode(ModelInternalNodeKey key) : key_{std::move(key)} {}

    /** Return the model-local node key. */
    [[nodiscard]] const ModelInternalNodeKey &key() const noexcept { return key_; }

  private:
    ModelInternalNodeKey key_;
};

/** Known nominal quantity, normalized optional uncertainty, and immutable evidence. */
class ModelParameter {
  public:
    /** Normalize tolerance to absolute deviations without treating absence as zero. */
    explicit ModelParameter(Quantity nominal, std::optional<Tolerance> tolerance = std::nullopt,
                            std::vector<ContentHash> evidence = {});

    /** Return the nominal quantity with normalized signed zero. */
    [[nodiscard]] const Quantity &nominal() const noexcept { return nominal_; }

    /** Return optional absolute deviations in the nominal dimension. */
    [[nodiscard]] const std::optional<Tolerance> &tolerance() const noexcept { return tolerance_; }

    /** Derive inclusive finite bounds, or no bounds for unspecified uncertainty. */
    [[nodiscard]] std::optional<QuantityRange> bounds() const;

    /** Return sorted unique immutable evidence content hashes. */
    [[nodiscard]] const std::vector<ContentHash> &evidence() const noexcept { return evidence_; }

  private:
    Quantity nominal_;
    std::optional<Tolerance> tolerance_;
    std::vector<ContentHash> evidence_;
};

/** Oriented ideal resistance law v - R i = 0; R=0 is an explicit voltage constraint. */
class ResistanceElement {
  public:
    /** Construct distinct endpoints with finite nonnegative resistance and bounds. */
    ResistanceElement(ModelElementKey key, ModelEndpoint from, ModelEndpoint to,
                      ModelParameter parameter);

    /** Return the stable observation key. */
    [[nodiscard]] const ModelElementKey &key() const noexcept { return key_; }

    /** Return the positive-voltage, outgoing-current endpoint. */
    [[nodiscard]] const ModelEndpoint &from() const noexcept { return from_; }

    /** Return the negative-voltage, incoming-current endpoint. */
    [[nodiscard]] const ModelEndpoint &to() const noexcept { return to_; }

    /** Return the nominal resistance and its uncertainty/evidence. */
    [[nodiscard]] const ModelParameter &parameter() const noexcept { return parameter_; }

  private:
    ModelElementKey key_;
    ModelEndpoint from_;
    ModelEndpoint to_;
    ModelParameter parameter_;
};

/** Oriented ideal capacitance law i - C dv/dt = 0. */
class CapacitanceElement {
  public:
    /** Construct distinct endpoints with finite positive capacitance and bounds. */
    CapacitanceElement(ModelElementKey key, ModelEndpoint from, ModelEndpoint to,
                       ModelParameter parameter);

    /** Return the stable observation key. */
    [[nodiscard]] const ModelElementKey &key() const noexcept { return key_; }

    /** Return the positive-voltage, outgoing-current endpoint. */
    [[nodiscard]] const ModelEndpoint &from() const noexcept { return from_; }

    /** Return the negative-voltage, incoming-current endpoint. */
    [[nodiscard]] const ModelEndpoint &to() const noexcept { return to_; }

    /** Return the nominal capacitance and its uncertainty/evidence. */
    [[nodiscard]] const ModelParameter &parameter() const noexcept { return parameter_; }

  private:
    ModelElementKey key_;
    ModelEndpoint from_;
    ModelEndpoint to_;
    ModelParameter parameter_;
};

/** Oriented ideal inductance law v - L di/dt = 0. */
class InductanceElement {
  public:
    /** Construct distinct endpoints with finite positive inductance and bounds. */
    InductanceElement(ModelElementKey key, ModelEndpoint from, ModelEndpoint to,
                      ModelParameter parameter);

    /** Return the stable observation key. */
    [[nodiscard]] const ModelElementKey &key() const noexcept { return key_; }

    /** Return the positive-voltage, outgoing-current endpoint. */
    [[nodiscard]] const ModelEndpoint &from() const noexcept { return from_; }

    /** Return the negative-voltage, incoming-current endpoint. */
    [[nodiscard]] const ModelEndpoint &to() const noexcept { return to_; }

    /** Return the nominal inductance and its uncertainty/evidence. */
    [[nodiscard]] const ModelParameter &parameter() const noexcept { return parameter_; }

  private:
    ModelElementKey key_;
    ModelEndpoint from_;
    ModelEndpoint to_;
    ModelParameter parameter_;
};

/** Closed passive element vocabulary. */
using ModelElement = std::variant<ResistanceElement, CapacitanceElement, InductanceElement>;

/** Complete immutable passive model implementing exactly one component contract. */
class PartElectricalModel {
  public:
    /** Validate portable model content and canonicalize each collection by typed key. */
    PartElectricalModel(const ComponentDefinition &component, std::vector<ModelTerminal> terminals,
                        std::vector<ModelInternalNode> internal_nodes,
                        std::vector<ModelElement> elements);

    /** Return the exact implemented component digest. */
    [[nodiscard]] const ContentHash &implemented_component() const noexcept {
        return implemented_component_;
    }

    /** Return the complete terminal-to-PinKey bijection in stable key order. */
    [[nodiscard]] const std::vector<ModelTerminal> &terminals() const noexcept {
        return terminals_;
    }

    /** Return private nodes in stable key order. */
    [[nodiscard]] const std::vector<ModelInternalNode> &internal_nodes() const noexcept {
        return internal_nodes_;
    }

    /** Return all oriented elements in stable key order. */
    [[nodiscard]] const std::vector<ModelElement> &elements() const noexcept { return elements_; }

  private:
    ContentHash implemented_component_;
    std::vector<ModelTerminal> terminals_;
    std::vector<ModelInternalNode> internal_nodes_;
    std::vector<ModelElement> elements_;
};

/** Append-only authoring of a complete passive model using builder-owned handles. */
class PartElectricalModelBuilder {
    struct Identity {};

  public:
    /** Typed reference to a declared terminal or internal node owned by one builder. */
    template <typename Key>
        requires(std::same_as<Key, ModelTerminalKey> || std::same_as<Key, ModelInternalNodeKey>)
    class Handle {
      public:
        /** Resolve an already declared key against this exact builder. */
        Handle(const PartElectricalModelBuilder &builder, Key key)
            : owner_{builder.identity_}, key_{std::move(key)} {
            builder.require_endpoint(ModelEndpoint{key_});
        }

        /** Return the stable portable identity without the builder ownership token. */
        [[nodiscard]] const Key &key() const noexcept { return key_; }

        /** Return whether this handle belongs to the specified builder. */
        [[nodiscard]] bool belongs_to(const PartElectricalModelBuilder &builder) const noexcept {
            return owner_ == builder.identity_;
        }

      private:
        std::shared_ptr<const Identity> owner_;
        Key key_;
    };

    /** Builder-owned terminal reference, distinct from an internal-node reference. */
    using TerminalHandle = Handle<ModelTerminalKey>;
    /** Builder-owned private-node reference. */
    using InternalNodeHandle = Handle<ModelInternalNodeKey>;
    /** Closed authoring endpoint retaining builder ownership. */
    using Endpoint = std::variant<TerminalHandle, InternalNodeHandle>;

    /** Begin authoring against an owned snapshot of an immutable component definition. */
    explicit PartElectricalModelBuilder(ComponentDefinition component);
    /** A builder identity is unique and cannot be copied. */
    PartElectricalModelBuilder(const PartElectricalModelBuilder &) = delete;
    /** A builder identity is unique and cannot be replaced. */
    PartElectricalModelBuilder &operator=(const PartElectricalModelBuilder &) = delete;
    /** A builder identity remains attached to its original builder. */
    PartElectricalModelBuilder(PartElectricalModelBuilder &&) = delete;
    /** A builder identity remains attached to its original builder. */
    PartElectricalModelBuilder &operator=(PartElectricalModelBuilder &&) = delete;

    /** Declare a unique model terminal for an as-yet unbound contract pin. */
    [[nodiscard]] TerminalHandle terminal(ModelTerminalKey key, PinKey pin);
    /** Declare a unique private model node. */
    [[nodiscard]] InternalNodeHandle internal_node(ModelInternalNodeKey key);

    /** Append one typed R/C/L element over endpoints owned by this builder. */
    template <typename Element>
        requires(std::same_as<Element, ResistanceElement> ||
                 std::same_as<Element, CapacitanceElement> ||
                 std::same_as<Element, InductanceElement>)
    PartElectricalModelBuilder &add(ModelElementKey key, Endpoint from, Endpoint to,
                                    ModelParameter parameter) {
        append(Element{std::move(key), resolve(from), resolve(to), std::move(parameter)});
        return *this;
    }

    /** Finalize a validated portable value independent of all builder lifetimes. */
    [[nodiscard]] PartElectricalModel build() const;

  private:
    void require_endpoint(const ModelEndpoint &endpoint) const;
    [[nodiscard]] ModelEndpoint resolve(const Endpoint &endpoint) const;
    void append(ModelElement element);

    ComponentDefinition component_;
    std::shared_ptr<const Identity> identity_;
    std::vector<ModelTerminal> terminals_;
    std::vector<ModelInternalNode> internal_nodes_;
    std::vector<ModelElement> elements_;
};

} // namespace volt
