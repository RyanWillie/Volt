#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <volt/circuit/circuit.hpp>
#include <volt/schematic/geometry.hpp>
#include <volt/schematic/schematic_items.hpp>
#include <volt/schematic/schematic_sheet.hpp>
#include <volt/schematic/schematic_sheet_metadata.hpp>
#include <volt/schematic/symbols.hpp>
#include <volt/schematic/updates.hpp>
#include <volt/schematic/wire_topology.hpp>

namespace volt {

class Schematic;

/// @cond
namespace detail {

struct SchematicItemsState;
struct SchematicLibraryState;
struct SchematicSheetState;

template <typename Id> struct SchematicEntityDescriptor;
template <typename Id> class SchematicEntityRange;

template <> struct SchematicEntityDescriptor<SymbolDefId> {
    using type = SymbolDefinition;
};

template <> struct SchematicEntityDescriptor<SheetId> {
    using type = Sheet;
};

template <> struct SchematicEntityDescriptor<SheetRegionId> {
    using type = SheetRegion;
};

template <> struct SchematicEntityDescriptor<SymbolInstanceId> {
    using type = SymbolInstance;
};

template <> struct SchematicEntityDescriptor<WireRunId> {
    using type = WireRun;
};

template <> struct SchematicEntityDescriptor<NetLabelId> {
    using type = NetLabel;
};

template <> struct SchematicEntityDescriptor<JunctionId> {
    using type = Junction;
};

template <> struct SchematicEntityDescriptor<PowerPortId> {
    using type = PowerPort;
};

template <> struct SchematicEntityDescriptor<NoConnectMarkerId> {
    using type = NoConnectMarker;
};

template <> struct SchematicEntityDescriptor<SheetPortId> {
    using type = SheetPort;
};

template <> struct SchematicEntityDescriptor<SymbolFieldId> {
    using type = SymbolField;
};

} // namespace detail

/// @endcond

/** True when an ID names one of Schematic's canonical presentation entity families. */
template <typename Id>
concept SchematicEntityId =
    std::same_as<Id, SymbolDefId> || std::same_as<Id, SheetId> || std::same_as<Id, SheetRegionId> ||
    std::same_as<Id, SymbolInstanceId> || std::same_as<Id, WireRunId> ||
    std::same_as<Id, NetLabelId> || std::same_as<Id, JunctionId> || std::same_as<Id, PowerPortId> ||
    std::same_as<Id, NoConnectMarkerId> || std::same_as<Id, SheetPortId> ||
    std::same_as<Id, SymbolFieldId>;

/** Canonical presentation entity type selected by a Schematic-owned stable ID. */
template <SchematicEntityId Id>
using schematic_entity_type_t = typename detail::SchematicEntityDescriptor<Id>::type;

/** Borrowed deterministic range selected by a Schematic-owned stable ID. */
template <SchematicEntityId Id> using schematic_entity_range_t = detail::SchematicEntityRange<Id>;

/**
 * Kernel-owned presentation projection over one logical circuit.
 *
 * Schematic owns presentation only. It references existing logical components, pins, and nets
 * and cannot create, merge, split, connect, disconnect, or reinterpret Circuit connectivity.
 */
class Schematic final {
  public:
    /** Construct a schematic projection for one logical circuit context. */
    explicit Schematic(const Circuit &circuit);

    /** Reject temporary circuit bindings because Schematic stores a caller-owned circuit reference.
     */
    explicit Schematic(const Circuit &&circuit) = delete;

    /** Store a reusable symbol definition and return its stable schematic ID. */
    [[nodiscard]] SymbolDefId add_symbol_definition(SymbolDefinition definition);

    /** Store a schematic sheet and return its stable schematic ID. */
    [[nodiscard]] SheetId add_sheet(Sheet sheet);

    /** Add a named presentation region to a schematic sheet. */
    [[nodiscard]] SheetRegionId add_sheet_region(SheetId sheet, SheetRegion region);

    /** Place a symbol on a sheet for an existing logical component instance. */
    [[nodiscard]] SymbolInstanceId place_symbol(SheetId sheet, SymbolInstance instance);

    /** Add an explicit junction over an existing logical net. */
    [[nodiscard]] JunctionId add_junction(SheetId sheet, Junction junction);

    /** Add a one-terminal marker over an existing logical net. */
    [[nodiscard]] PowerPortId add_power_port(SheetId sheet, PowerPort port);

    /** Add a no-connect marker for an existing concrete pin. */
    [[nodiscard]] NoConnectMarkerId add_no_connect_marker(SheetId sheet, NoConnectMarker marker);

    /** Add a sheet/off-page port over an existing logical net. */
    [[nodiscard]] SheetPortId add_sheet_port(SheetId sheet, SheetPort port);

    /** Add a placed field for a symbol instance on the same sheet. */
    [[nodiscard]] SymbolFieldId add_symbol_field(SheetId sheet, SymbolField field);

    /** Add a wire run on a sheet for an existing logical net. */
    [[nodiscard]] WireRunId add_wire_run(SheetId sheet, WireRun wire);

    /** Add a net label on a sheet for an existing logical net. */
    [[nodiscard]] NetLabelId add_net_label(SheetId sheet, NetLabel label);

    /** Apply one closed typed presentation move. */
    void move(SchematicMove change);

    /** Return a canonical presentation entity selected by its strongly typed stable ID. */
    template <SchematicEntityId Id>
    [[nodiscard]] const schematic_entity_type_t<Id> &get(Id id) const;

    /** Return a borrowed deterministic range over one presentation entity family. */
    template <SchematicEntityId Id> [[nodiscard]] schematic_entity_range_t<Id> all() const &;
    template <SchematicEntityId Id>
    [[nodiscard]] schematic_entity_range_t<Id> all() const && = delete;

    /** Return the logical circuit this schematic projection references. */
    [[nodiscard]] const Circuit &circuit() const noexcept { return circuit_; }

  private:
    struct LibraryStorage {
        LibraryStorage();
        LibraryStorage(const LibraryStorage &other);
        LibraryStorage(LibraryStorage &&other) noexcept = default;
        LibraryStorage &operator=(const LibraryStorage &other);
        LibraryStorage &operator=(LibraryStorage &&other) noexcept = default;

        [[nodiscard]] SymbolDefId add_symbol_definition(SymbolDefinition definition);
        [[nodiscard]] const SymbolDefinition &get(SymbolDefId id) const;
        [[nodiscard]] std::size_t size() const noexcept;
        void require(SymbolDefId id) const;

      private:
        explicit LibraryStorage(std::shared_ptr<detail::SchematicLibraryState> state);
        [[nodiscard]] detail::SchematicLibraryState &mutable_state() noexcept;
        [[nodiscard]] const detail::SchematicLibraryState &state() const noexcept;

        std::shared_ptr<detail::SchematicLibraryState> state_;
    };

    struct SheetStorage {
        SheetStorage();
        SheetStorage(const SheetStorage &other);
        SheetStorage(SheetStorage &&other) noexcept = default;
        SheetStorage &operator=(const SheetStorage &other);
        SheetStorage &operator=(SheetStorage &&other) noexcept = default;

        [[nodiscard]] SheetId add_sheet(Sheet sheet);
        [[nodiscard]] SheetRegionId add_sheet_region(SheetId sheet, SheetRegion region);
        void add_symbol_instance(SheetId sheet, SymbolInstanceId instance);
        void add_wire_run(SheetId sheet, WireRunId wire);
        void add_net_label(SheetId sheet, NetLabelId label);
        void add_junction(SheetId sheet, JunctionId junction);
        void add_power_port(SheetId sheet, PowerPortId port);
        void add_no_connect_marker(SheetId sheet, NoConnectMarkerId marker);
        void add_sheet_port(SheetId sheet, SheetPortId port);
        void add_symbol_field(SheetId sheet, SymbolFieldId field);
        [[nodiscard]] const Sheet &get(SheetId id) const;
        [[nodiscard]] const SheetRegion &get(SheetRegionId id) const;
        [[nodiscard]] std::size_t size(SheetId) const noexcept;
        [[nodiscard]] std::size_t size(SheetRegionId) const noexcept;
        void require(SheetId id) const;

      private:
        explicit SheetStorage(std::shared_ptr<detail::SchematicSheetState> state);
        [[nodiscard]] detail::SchematicSheetState &mutable_state() noexcept;
        [[nodiscard]] const detail::SchematicSheetState &state() const noexcept;

        std::shared_ptr<detail::SchematicSheetState> state_;
    };

    struct ItemStorage {
        ItemStorage();
        ItemStorage(const ItemStorage &other);
        ItemStorage(ItemStorage &&other) noexcept = default;
        ItemStorage &operator=(const ItemStorage &other);
        ItemStorage &operator=(ItemStorage &&other) noexcept = default;

        [[nodiscard]] SymbolInstanceId add(SymbolInstance instance);
        [[nodiscard]] WireRunId add(WireRun wire);
        [[nodiscard]] NetLabelId add(NetLabel label);
        [[nodiscard]] JunctionId add(Junction junction);
        [[nodiscard]] PowerPortId add(PowerPort port);
        [[nodiscard]] NoConnectMarkerId add(NoConnectMarker marker);
        [[nodiscard]] SheetPortId add(SheetPort port);
        [[nodiscard]] SymbolFieldId add(SymbolField field);

        [[nodiscard]] const SymbolInstance &get(SymbolInstanceId id) const;
        [[nodiscard]] const WireRun &get(WireRunId id) const;
        [[nodiscard]] const NetLabel &get(NetLabelId id) const;
        [[nodiscard]] const Junction &get(JunctionId id) const;
        [[nodiscard]] const PowerPort &get(PowerPortId id) const;
        [[nodiscard]] const NoConnectMarker &get(NoConnectMarkerId id) const;
        [[nodiscard]] const SheetPort &get(SheetPortId id) const;
        [[nodiscard]] const SymbolField &get(SymbolFieldId id) const;

        [[nodiscard]] std::size_t size(SymbolInstanceId) const noexcept;
        [[nodiscard]] std::size_t size(WireRunId) const noexcept;
        [[nodiscard]] std::size_t size(NetLabelId) const noexcept;
        [[nodiscard]] std::size_t size(JunctionId) const noexcept;
        [[nodiscard]] std::size_t size(PowerPortId) const noexcept;
        [[nodiscard]] std::size_t size(NoConnectMarkerId) const noexcept;
        [[nodiscard]] std::size_t size(SheetPortId) const noexcept;
        [[nodiscard]] std::size_t size(SymbolFieldId) const noexcept;

        void move(MoveNetLabelText change);
        void move(MovePowerPortLabel change);
        void move(MoveSymbolField change);
        void require(SymbolInstanceId id) const;

      private:
        explicit ItemStorage(std::shared_ptr<detail::SchematicItemsState> state);
        [[nodiscard]] detail::SchematicItemsState &mutable_state() noexcept;
        [[nodiscard]] const detail::SchematicItemsState &state() const noexcept;

        std::shared_ptr<detail::SchematicItemsState> state_;
    };

    void require_authored_region(SheetId sheet, const std::optional<std::size_t> &region) const;
    void require_symbol_matches_component(SymbolDefId symbol_definition,
                                          ComponentId component) const;
    [[nodiscard]] bool sheet_contains_symbol_instance(SheetId sheet,
                                                      SymbolInstanceId instance) const;
    [[nodiscard]] static bool wire_contains_point(const WireRun &wire, Point point);
    [[nodiscard]] bool has_junction_on_segments(SheetId sheet, SchematicSegment first,
                                                SchematicSegment second) const;
    void require_junction_does_not_touch_different_net(SheetId sheet,
                                                       const Junction &junction) const;
    void require_wire_run_does_not_collide_with_different_net(SheetId sheet,
                                                              const WireRun &wire) const;

    const Circuit &circuit_;
    LibraryStorage library_;
    SheetStorage sheets_;
    ItemStorage items_;
};

/// @cond
namespace detail {

template <typename Id> class SchematicEntityRange {
  public:
    class iterator {
      public:
        using value_type = schematic_entity_type_t<Id>;
        using difference_type = std::ptrdiff_t;
        using reference = const value_type &;
        using pointer = const value_type *;
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;

        iterator() = default;

        [[nodiscard]] reference operator*() const { return schematic_->get(Id{index_}); }

        [[nodiscard]] pointer operator->() const { return &**this; }

        iterator &operator++() {
            ++index_;
            return *this;
        }

        iterator operator++(int) {
            auto previous = *this;
            ++*this;
            return previous;
        }

        friend bool operator==(const iterator &, const iterator &) = default;

        iterator(const Schematic &schematic, std::size_t index) noexcept
            : schematic_{&schematic}, index_{index} {}

        iterator(const Schematic &&, std::size_t) = delete;

      private:
        const Schematic *schematic_ = nullptr;
        std::size_t index_ = 0;
    };

    [[nodiscard]] iterator begin() const noexcept { return iterator{*schematic_, 0}; }

    [[nodiscard]] iterator end() const noexcept { return iterator{*schematic_, size_}; }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

  private:
    friend schematic_entity_range_t<Id> Schematic::all<Id>() const &;

    SchematicEntityRange(const Schematic &schematic, std::size_t size) noexcept
        : schematic_{&schematic}, size_{size} {}

    const Schematic *schematic_;
    std::size_t size_ = 0;
};

} // namespace detail

/// @endcond

template <SchematicEntityId Id>
[[nodiscard]] const schematic_entity_type_t<Id> &Schematic::get(Id id) const {
    if constexpr (std::same_as<Id, SymbolDefId>) {
        return library_.get(id);
    } else if constexpr (std::same_as<Id, SheetId> || std::same_as<Id, SheetRegionId>) {
        return sheets_.get(id);
    } else {
        return items_.get(id);
    }
}

template <SchematicEntityId Id>
[[nodiscard]] schematic_entity_range_t<Id> Schematic::all() const & {
    std::size_t size = 0;
    if constexpr (std::same_as<Id, SymbolDefId>) {
        size = library_.size();
    } else if constexpr (std::same_as<Id, SheetId> || std::same_as<Id, SheetRegionId>) {
        size = sheets_.size(Id{0});
    } else {
        size = items_.size(Id{0});
    }
    return schematic_entity_range_t<Id>{*this, size};
}

} // namespace volt
