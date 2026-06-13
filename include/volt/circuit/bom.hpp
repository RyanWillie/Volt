#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/properties.hpp>

namespace volt {

/** Design-owned sourcing metadata keyed by manufacturer part number. */
class BomSourcingSnapshot {
  public:
    /** Set or replace deterministic sourcing fields for an MPN. */
    void set_mpn_properties(std::string mpn, PropertyMap properties);

    /** Return sourcing fields for an MPN, if present. */
    [[nodiscard]] const PropertyMap *properties_for_mpn(const std::string &mpn) const noexcept;

  private:
    std::vector<std::pair<std::string, PropertyMap>> entries_;
};

/** Selected physical part data projected into one BOM component row. */
class BomSelectedPart {
  public:
    /** Construct selected-part BOM data. */
    BomSelectedPart(std::string manufacturer, std::string mpn, std::string package,
                    std::vector<std::string> approved_alternate_mpns);

    /** Return manufacturer name. */
    [[nodiscard]] const std::string &manufacturer() const noexcept { return manufacturer_; }

    /** Return manufacturer part number. */
    [[nodiscard]] const std::string &mpn() const noexcept { return mpn_; }

    /** Return selected package label. */
    [[nodiscard]] const std::string &package() const noexcept { return package_; }

    /** Return approved alternate MPNs. */
    [[nodiscard]] const std::vector<std::string> &approved_alternate_mpns() const noexcept {
        return approved_alternate_mpns_;
    }

  private:
    std::string manufacturer_;
    std::string mpn_;
    std::string package_;
    std::vector<std::string> approved_alternate_mpns_;
};

/** One component instance row in the BOM projection. */
class BomComponent {
  public:
    /** Construct a projected BOM component row. */
    BomComponent(ComponentId component, std::string reference, bool dnp, bool dnp_explicit,
                 bool selection_override, std::optional<BomSelectedPart> selected_part);

    /** Return the component entity ID. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return the reference designator. */
    [[nodiscard]] const std::string &reference() const noexcept { return reference_; }

    /** Return whether the component is marked DNP for assembly. */
    [[nodiscard]] bool dnp() const noexcept { return dnp_; }

    /** Return whether DNP intent was explicitly authored. */
    [[nodiscard]] bool dnp_explicit() const noexcept { return dnp_explicit_; }

    /** Return whether this instance has selected-part override intent. */
    [[nodiscard]] bool selection_override() const noexcept { return selection_override_; }

    /** Return selected physical part data, when present. */
    [[nodiscard]] const std::optional<BomSelectedPart> &selected_part() const noexcept {
        return selected_part_;
    }

  private:
    ComponentId component_;
    std::string reference_;
    bool dnp_;
    bool dnp_explicit_;
    bool selection_override_;
    std::optional<BomSelectedPart> selected_part_;
};

/** One grouped orderable BOM line. */
class BomLine {
  public:
    /** Construct a grouped BOM line. */
    BomLine(std::string manufacturer, std::string mpn, std::string package, bool dnp,
            std::size_t quantity, std::vector<std::string> references,
            std::vector<std::string> approved_alternate_mpns,
            std::vector<std::string> selection_override_references, PropertyMap sourcing);

    /** Return manufacturer name. */
    [[nodiscard]] const std::string &manufacturer() const noexcept { return manufacturer_; }

    /** Return manufacturer part number. */
    [[nodiscard]] const std::string &mpn() const noexcept { return mpn_; }

    /** Return package label. */
    [[nodiscard]] const std::string &package() const noexcept { return package_; }

    /** Return whether this line is do-not-populate. */
    [[nodiscard]] bool dnp() const noexcept { return dnp_; }

    /** Return purchase quantity; DNP lines have quantity zero. */
    [[nodiscard]] std::size_t quantity() const noexcept { return quantity_; }

    /** Return grouped reference designators. */
    [[nodiscard]] const std::vector<std::string> &references() const noexcept {
        return references_;
    }

    /** Return approved alternate MPNs for this line. */
    [[nodiscard]] const std::vector<std::string> &approved_alternate_mpns() const noexcept {
        return approved_alternate_mpns_;
    }

    /** Return references whose selected part is marked as an override. */
    [[nodiscard]] const std::vector<std::string> &selection_override_references() const noexcept {
        return selection_override_references_;
    }

    /** Return sourcing fields merged by MPN. */
    [[nodiscard]] const PropertyMap &sourcing() const noexcept { return sourcing_; }

  private:
    std::string manufacturer_;
    std::string mpn_;
    std::string package_;
    bool dnp_;
    std::size_t quantity_;
    std::vector<std::string> references_;
    std::vector<std::string> approved_alternate_mpns_;
    std::vector<std::string> selection_override_references_;
    PropertyMap sourcing_;
};

/** Complete deterministic BOM projection over a logical circuit. */
class Bom {
  public:
    /** Construct a BOM projection from component rows and grouped lines. */
    Bom(std::vector<BomComponent> components, std::vector<BomLine> lines);

    /** Return component rows sorted by reference designator. */
    [[nodiscard]] const std::vector<BomComponent> &components() const noexcept {
        return components_;
    }

    /** Return grouped orderable lines sorted by stable orderable keys. */
    [[nodiscard]] const std::vector<BomLine> &lines() const noexcept { return lines_; }

  private:
    std::vector<BomComponent> components_;
    std::vector<BomLine> lines_;
};

/** Project a circuit into a deterministic BOM without sourcing metadata. */
[[nodiscard]] Bom project_bom(const Circuit &circuit);

/** Project a circuit into a deterministic BOM and merge sourcing by MPN. */
[[nodiscard]] Bom project_bom(const Circuit &circuit, const BomSourcingSnapshot &sourcing);

} // namespace volt
