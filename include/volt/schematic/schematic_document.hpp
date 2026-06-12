#pragma once

#include <utility>

#include <volt/schematic/schematic.hpp>

namespace volt {

/** First-class schematic document artifact over one logical circuit context. */
class SchematicDocument {
  public:
    /** Construct an empty schematic document for a logical circuit. */
    explicit SchematicDocument(const Circuit &circuit);

    /** Reject temporary circuit bindings because the owned schematic stores a circuit reference. */
    explicit SchematicDocument(const Circuit &&circuit) = delete;

    /** Construct a schematic document from loaded projection data. */
    explicit SchematicDocument(Schematic schematic);

    /** Return the logical circuit this schematic document visualizes. */
    [[nodiscard]] const Circuit &circuit() const noexcept { return schematic_.circuit(); }

    /** Return the mutable kernel-owned schematic projection. */
    [[nodiscard]] Schematic &schematic() noexcept { return schematic_; }

    /** Return the kernel-owned schematic projection. */
    [[nodiscard]] const Schematic &schematic() const noexcept { return schematic_; }

    /** Replace the document contents with loaded projection data for the same circuit. */
    void replace_schematic(Schematic schematic);

  private:
    Schematic schematic_;
};

} // namespace volt
