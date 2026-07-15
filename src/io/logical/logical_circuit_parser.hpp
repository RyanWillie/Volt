#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/circuit.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/detail/typed_id.hpp>

#include "logical_circuit_restoration.hpp"

namespace volt::io::detail {

/** Parse and validate the v1 logical circuit JSON format without mutating Circuit. */
class LogicalCircuitParser {
  public:
    /** Construct a parser over a JSON document. */
    explicit LogicalCircuitParser(const nlohmann::json &document) : document_{document} {}

    /** Parse the complete document into one explicit restoration plan. */
    [[nodiscard]] LogicalCircuitRestorationPlan parse();

  private:
    struct ParsedComponentContract {
        ComponentContractSpec spec;
        ContentHash content_identity;
    };

    static void require(bool condition, const std::string &message);

    static const nlohmann::json &field(const nlohmann::json &object, const char *name);

    static std::string string_field(const nlohmann::json &object, const char *name);

    static std::string optional_string_field(const nlohmann::json &object, const char *name,
                                             std::string default_value);

    static void require_format(const nlohmann::json &object);

    static void require_version(const nlohmann::json &object);

    static const nlohmann::json &array_field(const nlohmann::json &object, const char *name);

    static const nlohmann::json *optional_array_field(const nlohmann::json &object,
                                                      const char *name);

    template <typename Id>
    static std::string local_id(const nlohmann::json &object, std::set<std::string> &seen) {
        const auto id = string_field(object, "id");
        static_cast<void>(decode_local_id<Id>(id));
        if (!seen.insert(id).second) {
            throw KernelLogicError{ErrorCode::DuplicateName, "Duplicate local ID"};
        }
        return id;
    }

    [[nodiscard]] static ConnectionRequirement connection_requirement(const std::string &value);

    [[nodiscard]] static ElectricalTerminalKind electrical_terminal_kind(const std::string &value);

    [[nodiscard]] static ElectricalDirection electrical_direction(const std::string &value);

    [[nodiscard]] static ElectricalSignalDomain electrical_signal_domain(const std::string &value);

    [[nodiscard]] static ElectricalDriveKind electrical_drive_kind(const std::string &value);

    [[nodiscard]] static ElectricalPolarity electrical_polarity(const std::string &value);

    [[nodiscard]] static NetKind net_kind(const std::string &value);

    [[nodiscard]] static PortRole port_role(const std::string &value);

    [[nodiscard]] static UnitDimension unit_dimension(const std::string &value);

    [[nodiscard]] static ToleranceMode tolerance_mode(const std::string &value);

    [[nodiscard]] static double number_field(const nlohmann::json &object, const char *name);

    [[nodiscard]] static PropertyValue property_value(const nlohmann::json &object);

    [[nodiscard]] static PropertyMap properties(const nlohmann::json &object);

    [[nodiscard]] static ElectricalAttributeValue
    electrical_attribute_value(const nlohmann::json &object);

    [[nodiscard]] static ElectricalAttributeMap
    electrical_attributes(const nlohmann::json &object, ElectricalAttributeOwner owner,
                          ElectricalAttributeKind kind);

    [[nodiscard]] static std::optional<DefinitionSource>
    definition_source(const nlohmann::json &object);

    [[nodiscard]] static std::vector<SchematicSymbolReference>
    schematic_symbol_references(const nlohmann::json &object);

    [[nodiscard]] static ElectricalSubjectKind component_subject_kind(const std::string &value);

    [[nodiscard]] static FeatureRoleCardinality feature_role_cardinality(const std::string &value);

    [[nodiscard]] static ElectricalObservable canonical_observable(const std::string &value);

    [[nodiscard]] static ElectricalMeaning canonical_meaning(const std::string &value);

    [[nodiscard]] static std::optional<ParsedComponentContract>
    component_contract(const nlohmann::json &object);

    template <typename Id>
    [[nodiscard]] Id resolve(const std::map<std::string, Id> &ids, const std::string &id) const {
        const auto it = ids.find(id);
        require(it != ids.end(), "Reference points to a missing local ID");
        return it->second;
    }

    void read_pin_definitions();

    void read_component_definitions();

    void read_components();

    void read_pins();

    void read_nets();

    void read_net_classes();

    void read_design_intent();

    void read_module_definitions();

    void read_module_instances();

    [[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
    infer_component_origins(ModuleDefId definition, const ModuleInstanceName &name) const;

    [[nodiscard]] PhysicalPart physical_part(const nlohmann::json &object) const;

    const nlohmann::json &document_;
    LogicalCircuitRestorationPlan plan_;
    std::vector<std::optional<ComponentDefId>> pin_definition_owners_;
    std::map<std::string, PinDefId> pin_def_ids_;
    std::map<std::string, ComponentDefId> component_def_ids_;
    std::map<std::string, ComponentId> component_ids_;
    std::map<std::string, ComponentId> component_reference_ids_;
    std::map<std::string, PinId> pin_ids_;
    std::map<std::string, NetId> net_ids_;
    std::map<std::string, NetClassId> net_class_ids_;
    std::map<std::string, ModuleDefId> module_def_ids_;
    std::map<std::string, TemplateNetDefId> template_net_ids_;
    std::map<std::string, ModuleComponentId> module_component_ids_;
    std::map<std::string, PortDefId> port_def_ids_;
};

} // namespace volt::io::detail
