#pragma once

#include <istream>
#include <string>
#include <string_view>

#include <volt/circuit/parts/part_definition.hpp>

namespace volt::io {

/** Validated legacy v4 artifact retained only as explicit migration input. */
class PartDefinitionV4 {
  public:
    /** Read and validate a legacy v4 part artifact from JSON text. */
    [[nodiscard]] static PartDefinitionV4 read_text(std::string_view text);

    /** Read and validate a legacy v4 part artifact from a JSON stream. */
    [[nodiscard]] static PartDefinitionV4 read(std::istream &input);

    /** Convert this v4 artifact using one exact component and explicit canonical P1 records. */
    [[nodiscard]] PartDefinition convert(const ComponentDefinition &component,
                                         ElectricalRecordSet electrical_records) const;

  private:
    explicit PartDefinitionV4(std::string normalized_json);

    std::string normalized_json_;
};

/** Read a current exact part artifact against the one component digest it must implement. */
[[nodiscard]] PartDefinition read_part_definition_text(std::string_view text,
                                                       const ComponentDefinition &component);

/** Read a current exact part artifact against the one component digest it must implement. */
[[nodiscard]] PartDefinition read_part_definition(std::istream &input,
                                                  const ComponentDefinition &component);

} // namespace volt::io
