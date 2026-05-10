#pragma once

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

  private:
    friend class Schematic;

    void add_symbol_instance(SymbolInstanceId instance) { symbol_instances_.push_back(instance); }

    std::string name_;
    std::vector<SymbolInstanceId> symbol_instances_;
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

/** Kernel-owned schematic projection over a logical circuit. */
class Schematic {
  public:
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
    [[nodiscard]] SymbolInstanceId place_symbol(const Circuit &circuit, SheetId sheet,
                                                SymbolInstance instance) {
        require_sheet(sheet);
        require_symbol_definition(instance.symbol_definition());
        static_cast<void>(circuit.component(instance.component()));

        const auto id = symbol_instances_.insert(std::move(instance));
        sheets_.get(sheet).add_symbol_instance(id);
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

    EntityTable<SymbolDefinition, SymbolDefId> symbol_definitions_;
    EntityTable<Sheet, SheetId> sheets_;
    EntityTable<SymbolInstance, SymbolInstanceId> symbol_instances_;
};

} // namespace volt
