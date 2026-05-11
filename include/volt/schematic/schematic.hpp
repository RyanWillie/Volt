#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/schematic/geometry.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt {

/** A schematic sheet that owns presentation objects for one drawing page. */
class Sheet {
  public:
    /** Construct a named schematic sheet. */
    explicit Sheet(std::string name) : name_{std::move(name)} {
        if (name_.empty()) {
            throw std::invalid_argument{"Sheet name must not be empty"};
        }
    }

    /** Return the sheet name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return placed symbol instances in insertion order. */
    [[nodiscard]] const std::vector<SymbolInstanceId> &symbol_instances() const noexcept {
        return symbol_instances_;
    }

    /** Return wire runs in insertion order. */
    [[nodiscard]] const std::vector<WireRunId> &wire_runs() const noexcept { return wire_runs_; }

    /** Return net labels in insertion order. */
    [[nodiscard]] const std::vector<NetLabelId> &net_labels() const noexcept { return net_labels_; }

  private:
    friend class Schematic;

    void add_symbol_instance(SymbolInstanceId instance) { symbol_instances_.push_back(instance); }

    void add_wire_run(WireRunId wire) { wire_runs_.push_back(wire); }

    void add_net_label(NetLabelId label) { net_labels_.push_back(label); }

    std::string name_;
    std::vector<SymbolInstanceId> symbol_instances_;
    std::vector<WireRunId> wire_runs_;
    std::vector<NetLabelId> net_labels_;
};

/** A placed schematic symbol that presents an existing logical component instance. */
class SymbolInstance {
  public:
    /** Construct a symbol instance over an existing logical component. */
    SymbolInstance(SymbolDefId symbol_definition, ComponentId component, Point position,
                   SchematicOrientation orientation = SchematicOrientation::Right)
        : symbol_definition_{symbol_definition}, component_{component}, position_{position},
          orientation_{orientation} {}

    /** Return the reusable symbol definition used by this placement. */
    [[nodiscard]] SymbolDefId symbol_definition() const noexcept { return symbol_definition_; }

    /** Return the logical component instance presented by this placement. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return the sheet-local symbol origin. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the symbol orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

  private:
    SymbolDefId symbol_definition_;
    ComponentId component_;
    Point position_;
    SchematicOrientation orientation_;
};

/** A drawn schematic wire segment sequence that presents one canonical logical net. */
class WireRun {
  public:
    /** Construct a wire run over an existing logical net. */
    WireRun(NetId net, std::vector<Point> points) : net_{net}, points_{std::move(points)} {
        if (points_.size() < 2U) {
            throw std::invalid_argument{"Schematic wire run must contain at least two points"};
        }
    }

    /** Return the canonical logical net presented by this wire run. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the wire polyline points in drawing order. */
    [[nodiscard]] const std::vector<Point> &points() const noexcept { return points_; }

  private:
    NetId net_;
    std::vector<Point> points_;
};

/** A schematic label whose visible text is derived from a canonical logical net name. */
class NetLabel {
  public:
    /** Construct a net label over an existing logical net. */
    NetLabel(NetId net, Point position,
             SchematicOrientation orientation = SchematicOrientation::Right)
        : net_{net}, position_{position}, orientation_{orientation} {}

    /** Return the canonical logical net named by this label. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the label anchor position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the label orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

  private:
    NetId net_;
    Point position_;
    SchematicOrientation orientation_;
};

/** Kernel-owned schematic projection over a logical circuit. */
class Schematic {
  public:
    /** Construct a schematic projection for one logical circuit context. */
    explicit Schematic(const Circuit &circuit) : circuit_{circuit} {}

    /** Store a reusable symbol definition and return its stable schematic ID. */
    [[nodiscard]] SymbolDefId add_symbol_definition(SymbolDefinition definition) {
        if (symbol_definition_by_name(definition.name()).has_value()) {
            throw std::logic_error{"Symbol definition name already exists"};
        }

        return symbol_definitions_.insert(std::move(definition));
    }

    /** Store a schematic sheet and return its stable schematic ID. */
    [[nodiscard]] SheetId add_sheet(Sheet sheet) {
        if (sheet_by_name(sheet.name()).has_value()) {
            throw std::logic_error{"Sheet name already exists"};
        }

        return sheets_.insert(std::move(sheet));
    }

    /** Place a symbol on a sheet for an existing logical component instance. */
    [[nodiscard]] SymbolInstanceId place_symbol(SheetId sheet, SymbolInstance instance) {
        require_sheet(sheet);
        require_symbol_definition(instance.symbol_definition());
        static_cast<void>(circuit_.component(instance.component()));

        const auto id = symbol_instances_.insert(std::move(instance));
        sheets_.get(sheet).add_symbol_instance(id);
        return id;
    }

    /** Add a wire run on a sheet for an existing logical net. */
    [[nodiscard]] WireRunId add_wire_run(SheetId sheet, WireRun wire) {
        require_sheet(sheet);
        static_cast<void>(circuit_.net(wire.net()));
        require_wire_run_does_not_join_different_net(sheet, wire);

        const auto id = wire_runs_.insert(std::move(wire));
        sheets_.get(sheet).add_wire_run(id);
        return id;
    }

    /** Add a net label on a sheet for an existing logical net. */
    [[nodiscard]] NetLabelId add_net_label(SheetId sheet, NetLabel label) {
        require_sheet(sheet);
        static_cast<void>(circuit_.net(label.net()));

        const auto id = net_labels_.insert(std::move(label));
        sheets_.get(sheet).add_net_label(id);
        return id;
    }

    /** Return the symbol definition with this name, if it exists. */
    [[nodiscard]] std::optional<SymbolDefId>
    symbol_definition_by_name(const std::string &name) const {
        for (std::size_t index = 0; index < symbol_definitions_.size(); ++index) {
            const auto id = SymbolDefId{index};
            if (symbol_definitions_.get(id).name() == name) {
                return id;
            }
        }

        return std::nullopt;
    }

    /** Return the sheet with this name, if it exists. */
    [[nodiscard]] std::optional<SheetId> sheet_by_name(const std::string &name) const {
        for (std::size_t index = 0; index < sheets_.size(); ++index) {
            const auto id = SheetId{index};
            if (sheets_.get(id).name() == name) {
                return id;
            }
        }

        return std::nullopt;
    }

    /** Return a symbol definition by ID. */
    [[nodiscard]] const SymbolDefinition &symbol_definition(SymbolDefId id) const {
        return symbol_definitions_.get(id);
    }

    /** Return a schematic sheet by ID. */
    [[nodiscard]] const Sheet &sheet(SheetId id) const { return sheets_.get(id); }

    /** Return a placed symbol instance by ID. */
    [[nodiscard]] const SymbolInstance &symbol_instance(SymbolInstanceId id) const {
        return symbol_instances_.get(id);
    }

    /** Return a wire run by ID. */
    [[nodiscard]] const WireRun &wire_run(WireRunId id) const { return wire_runs_.get(id); }

    /** Return a net label by ID. */
    [[nodiscard]] const NetLabel &net_label(NetLabelId id) const { return net_labels_.get(id); }

    /** Return the logical circuit this schematic projection references. */
    [[nodiscard]] const Circuit &circuit() const noexcept { return circuit_; }

    /** Return the number of stored symbol definitions. */
    [[nodiscard]] std::size_t symbol_definition_count() const noexcept {
        return symbol_definitions_.size();
    }

    /** Return the number of stored sheets. */
    [[nodiscard]] std::size_t sheet_count() const noexcept { return sheets_.size(); }

    /** Return the number of stored symbol instances. */
    [[nodiscard]] std::size_t symbol_instance_count() const noexcept {
        return symbol_instances_.size();
    }

    /** Return the number of stored wire runs. */
    [[nodiscard]] std::size_t wire_run_count() const noexcept { return wire_runs_.size(); }

    /** Return the number of stored net labels. */
    [[nodiscard]] std::size_t net_label_count() const noexcept { return net_labels_.size(); }

  private:
    void require_sheet(SheetId sheet) const {
        if (!sheets_.contains(sheet)) {
            throw std::out_of_range{"Sheet ID does not belong to this schematic"};
        }
    }

    void require_symbol_definition(SymbolDefId symbol_definition) const {
        if (!symbol_definitions_.contains(symbol_definition)) {
            throw std::out_of_range{"Symbol definition ID does not belong to this schematic"};
        }
    }

    [[nodiscard]] static double cross(Point origin, Point a, Point b) noexcept {
        return ((a.x() - origin.x()) * (b.y() - origin.y())) -
               ((a.y() - origin.y()) * (b.x() - origin.x()));
    }

    [[nodiscard]] static bool near_zero(double value) noexcept { return std::abs(value) <= 1e-9; }

    [[nodiscard]] static bool between(double value, double first, double second) noexcept {
        const auto minimum = std::min(first, second) - 1e-9;
        const auto maximum = std::max(first, second) + 1e-9;
        return minimum <= value && value <= maximum;
    }

    [[nodiscard]] static bool point_on_segment(Point point, Point start, Point end) noexcept {
        return near_zero(cross(start, end, point)) && between(point.x(), start.x(), end.x()) &&
               between(point.y(), start.y(), end.y());
    }

    [[nodiscard]] static bool segments_intersect(Point first_start, Point first_end,
                                                 Point second_start, Point second_end) noexcept {
        if (point_on_segment(second_start, first_start, first_end) ||
            point_on_segment(second_end, first_start, first_end) ||
            point_on_segment(first_start, second_start, second_end) ||
            point_on_segment(first_end, second_start, second_end)) {
            return true;
        }

        const auto first_side_start = cross(first_start, first_end, second_start);
        const auto first_side_end = cross(first_start, first_end, second_end);
        const auto second_side_start = cross(second_start, second_end, first_start);
        const auto second_side_end = cross(second_start, second_end, first_end);

        return ((first_side_start > 0.0 && first_side_end < 0.0) ||
                (first_side_start < 0.0 && first_side_end > 0.0)) &&
               ((second_side_start > 0.0 && second_side_end < 0.0) ||
                (second_side_start < 0.0 && second_side_end > 0.0));
    }

    void require_wire_run_does_not_join_different_net(SheetId sheet, const WireRun &wire) const {
        for (const auto existing_id : sheets_.get(sheet).wire_runs()) {
            const auto &existing = wire_runs_.get(existing_id);
            if (existing.net() == wire.net()) {
                continue;
            }
            for (std::size_t wire_index = 1; wire_index < wire.points().size(); ++wire_index) {
                for (std::size_t existing_index = 1; existing_index < existing.points().size();
                     ++existing_index) {
                    if (segments_intersect(wire.points()[wire_index - 1U],
                                           wire.points()[wire_index],
                                           existing.points()[existing_index - 1U],
                                           existing.points()[existing_index])) {
                        throw std::logic_error{
                            "Schematic wire run visually joins a different logical net"};
                    }
                }
            }
        }
    }

    const Circuit &circuit_;
    EntityTable<SymbolDefinition, SymbolDefId> symbol_definitions_;
    EntityTable<Sheet, SheetId> sheets_;
    EntityTable<SymbolInstance, SymbolInstanceId> symbol_instances_;
    EntityTable<WireRun, WireRunId> wire_runs_;
    EntityTable<NetLabel, NetLabelId> net_labels_;
};

} // namespace volt
