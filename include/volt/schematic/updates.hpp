#pragma once

#include <variant>

#include <volt/core/ids.hpp>
#include <volt/schematic/geometry.hpp>

namespace volt {

/** Move rendered net-label text without changing its presentation anchor. */
struct MoveNetLabelText {
    /** Existing net label to update. */
    NetLabelId label;
    /** New rendered text position. */
    Point position;
};

/** Move a rendered power-port label without changing its marker anchor. */
struct MovePowerPortLabel {
    /** Existing power or ground port to update. */
    PowerPortId port;
    /** New rendered label position. */
    Point position;
};

/** Move a symbol field without changing its owning symbol instance. */
struct MoveSymbolField {
    /** Existing symbol field to update. */
    SymbolFieldId field;
    /** New field position. */
    Point position;
};

/** Closed set of presentation-only moves accepted by Schematic. */
using SchematicMove = std::variant<MoveNetLabelText, MovePowerPortLabel, MoveSymbolField>;

} // namespace volt
