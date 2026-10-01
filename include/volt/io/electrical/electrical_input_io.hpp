#pragma once

#include <volt/electrical/electrical_input.hpp>
#include <volt/library/part_library.hpp>

namespace volt {

/** Native IO preparation boundary for an owning exact electrical input. */
class ElectricalInput::Codec final {
  public:
    /** Capture canonical logical identity and resolve each selected exact Part once. */
    [[nodiscard]] static ElectricalInput prepare(const Circuit &circuit,
                                                 const PartDefinitionResolver &resolver);
};

} // namespace volt

namespace volt::io {

/** Capture an immutable logical/Part input using only the explicitly supplied resolver. */
[[nodiscard]] ElectricalInput prepare_electrical_input(const Circuit &circuit,
                                                       const PartDefinitionResolver &resolver);

} // namespace volt::io
