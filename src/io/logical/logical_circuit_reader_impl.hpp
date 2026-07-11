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

namespace volt::io::detail {

struct RestoredPinDefinition {
    PinDefinition definition;
    ElectricalAttributeMap electrical_attributes;
};

struct RestoredComponentDefinition {
    ComponentDefinition definition;
};

struct RestoredComponentInstance {
    ComponentInstance instance;
    ElectricalAttributeMap electrical_attributes;
};

struct ConnectivityRestoration {
    std::vector<RestoredPinDefinition> pin_definitions;
    std::vector<RestoredComponentDefinition> component_definitions;
    std::vector<RestoredComponentInstance> components;
    std::vector<PinInstance> pins;
};

struct RestoredModuleDefinition {
    ModuleDefId id;
    ModuleDefinition definition;
};

struct RestoredTemplateNetDefinition {
    TemplateNetDefId id;
    ModuleDefId module;
    TemplateNetDefinition definition;
};

struct RestoredModuleComponent {
    ModuleComponentId id;
    ModuleDefId module;
    ModuleComponentTemplate component;
};

struct RestoredModulePinConnection {
    ModuleDefId module;
    TemplateNetDefId net;
    ModuleComponentId component;
    PinDefId pin;
};

struct RestoredPortDefinition {
    PortDefId id;
    ModuleDefId module;
    PortDefinition definition;
};

struct HierarchyDefinitionRestoration {
    std::vector<RestoredModuleDefinition> module_definitions;
    std::vector<RestoredTemplateNetDefinition> template_nets;
    std::vector<RestoredModuleComponent> components;
    std::vector<RestoredModulePinConnection> connections;
    std::vector<RestoredPortDefinition> ports;
};

struct ModuleInstanceRestoration {
    ModuleDefId definition;
    ModuleInstanceName name;
    std::vector<std::pair<TemplateNetDefId, NetId>> net_origins;
    std::vector<std::pair<ModuleComponentId, ComponentId>> component_origins;
};

/** Internal implementation for loading the v1 logical circuit JSON format. */
class LogicalCircuitReader {
  public:
    /** Construct a reader over a parsed JSON document. */
    explicit LogicalCircuitReader(const nlohmann::json &document) : document_{document} {}

    /** Load and structurally validate the document into a Circuit. */
    [[nodiscard]] Circuit read();

  private:
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

    void read_component_electrical_attributes(const nlohmann::json &object, ComponentId component,
                                              ElectricalAttributeOwner owner);

    void read_net_electrical_attributes(const nlohmann::json &object, NetId net);

    [[nodiscard]] static std::optional<DefinitionSource>
    definition_source(const nlohmann::json &object);

    [[nodiscard]] static std::vector<SchematicSymbolReference>
    schematic_symbol_references(const nlohmann::json &object);

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

    void restore_connectivity();

    void read_nets();

    void read_net_classes();

    void read_design_intent();

    void read_module_definitions();

    void read_module_instances();

    [[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
    infer_component_origins(ModuleDefId definition, const ModuleInstanceName &name) const;

    [[nodiscard]] PhysicalPart physical_part(const nlohmann::json &object) const;

    void read_selected_physical_parts();

    const nlohmann::json &document_;
    Circuit circuit_;
    ConnectivityRestoration connectivity_restoration_;
    std::vector<std::optional<ComponentDefId>> pin_definition_owners_;
    std::map<std::string, PinDefId> pin_def_ids_;
    std::map<std::string, ComponentDefId> component_def_ids_;
    std::map<std::string, ComponentId> component_ids_;
    std::map<std::string, PinId> pin_ids_;
    std::map<std::string, NetId> net_ids_;
    std::map<std::string, NetClassId> net_class_ids_;
    std::map<std::string, ModuleDefId> module_def_ids_;
    std::map<std::string, TemplateNetDefId> template_net_ids_;
    std::map<std::string, ModuleComponentId> module_component_ids_;
    std::map<std::string, PortDefId> port_def_ids_;
    std::map<std::string, ModuleInstanceId> module_instance_ids_;
    std::vector<std::pair<std::string, nlohmann::json>> selected_parts_;
};

} // namespace volt::io::detail
