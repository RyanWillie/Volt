#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include <volt/circuit/part_definition.hpp>
#include <volt/core/diagnostics.hpp>

namespace {

volt::ContentHash hash(char fill) { return volt::ContentHash{"sha256:" + std::string(64U, fill)}; }

volt::PartPin pin(std::string name, std::string number) {
    return volt::PartPin{volt::PinDefinition{
        std::move(name), std::move(number), volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Passive, volt::ElectricalDirection::Passive}};
}

std::vector<volt::PartPin> three_pin_map() {
    return std::vector{pin("GND", "1"), pin("VO", "2"), pin("VI", "3")};
}

std::vector<volt::PartSymbolPin> three_symbol_pins() {
    return std::vector{
        volt::PartSymbolPin{"GND", "1"},
        volt::PartSymbolPin{"VO", "2"},
        volt::PartSymbolPin{"VI", "3"},
    };
}

volt::HashedSchematicSymbolReference
symbol(std::vector<volt::PartSymbolPin> pins = three_symbol_pins()) {
    return volt::HashedSchematicSymbolReference{"volt.power:regulator_3pin", "default", hash('a'),
                                                std::move(pins)};
}

std::vector<volt::PartFootprintPad> three_footprint_pads() {
    return std::vector{
        volt::PartFootprintPad{"1", -1.0, 0.0, 0.6, 0.6},
        volt::PartFootprintPad{"2", 0.0, 0.0, 0.6, 0.6},
        volt::PartFootprintPad{"3", 1.0, 0.0, 0.6, 0.6},
        volt::PartFootprintPad{"MH", 0.0, 2.0, 1.8, 1.8, volt::PartFootprintPadRole::Mechanical},
    };
}

std::vector<volt::OrderablePinPadMapping> three_mappings() {
    return std::vector{
        volt::OrderablePinPadMapping{"1", "1"},
        volt::OrderablePinPadMapping{"2", "2"},
        volt::OrderablePinPadMapping{"3", "3"},
    };
}

volt::OrderablePart orderable(std::vector<volt::OrderablePinPadMapping> mappings = three_mappings(),
                              std::vector<volt::PartFootprintPad> pads = three_footprint_pads()) {
    return volt::OrderablePart{
        volt::ManufacturerPart{"Diodes Incorporated", "AP1117E15G-13"},
        volt::PackageRef{"SOT-223-3"},
        volt::HashedFootprintReference{
            volt::FootprintRef{"Package_TO_SOT_SMD", "SOT-223-3_TabPin2"}, hash('b')},
        std::move(pads),
        std::move(mappings),
    };
}

volt::PartDefinition
part(std::vector<volt::PartPin> pins = three_pin_map(),
     std::vector<volt::HashedSchematicSymbolReference> symbols = std::vector{symbol()},
     volt::OrderablePart physical = orderable()) {
    return volt::PartDefinition{
        volt::PartIdentity{"volt.power", "AP1117-15", "1.0.0"},
        std::move(pins),
        {},
        volt::PartProvenance{},
        std::move(symbols),
        std::move(physical),
    };
}

const volt::Diagnostic *find_diagnostic(const volt::DiagnosticReport &report,
                                        std::string_view code) {
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code().value() == code) {
            return &diagnostic;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Part definition assembly rejects symbol pins outside the pin map") {
    CHECK_THROWS_AS(part(three_pin_map(), std::vector{symbol(std::vector{
                                              volt::PartSymbolPin{"GND", "1"},
                                              volt::PartSymbolPin{"VO", "2"},
                                              volt::PartSymbolPin{"ENABLE", "9"},
                                          })}),
                    std::invalid_argument);
}

TEST_CASE("Part footprint polygons reject structurally invalid geometry") {
    CHECK_THROWS_AS(volt::PartFootprintPolygon(std::vector<volt::PartFootprintPoint>{}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::PartFootprintPolygon(std::vector{
                        volt::PartFootprintPoint{0.0, 0.0},
                        volt::PartFootprintPoint{1.0, 0.0},
                    }),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::PartFootprintPolygon(std::vector{
                        volt::PartFootprintPoint{0.0, 0.0},
                        volt::PartFootprintPoint{1.0, 0.0},
                        volt::PartFootprintPoint{2.0, 0.0},
                    }),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::PartFootprintPolygon(std::vector{
                        volt::PartFootprintPoint{0.0, 0.0},
                        volt::PartFootprintPoint{1.0, 0.0},
                        volt::PartFootprintPoint{1.0, 0.0},
                        volt::PartFootprintPoint{0.0, 1.0},
                    }),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::PartFootprintPolygon(std::vector{
                        volt::PartFootprintPoint{0.0, 0.0},
                        volt::PartFootprintPoint{1.0, 0.0},
                        volt::PartFootprintPoint{0.0, 1.0},
                        volt::PartFootprintPoint{0.0, 0.0},
                        volt::PartFootprintPoint{-1.0, 0.0},
                    }),
                    std::invalid_argument);
}

TEST_CASE("Part definition assembly requires every pin to appear in each symbol exactly once") {
    CHECK_THROWS_AS(part(three_pin_map(), std::vector<volt::HashedSchematicSymbolReference>{}),
                    std::invalid_argument);

    CHECK_THROWS_AS(part(three_pin_map(), std::vector{symbol(std::vector{
                                              volt::PartSymbolPin{"GND", "1"},
                                              volt::PartSymbolPin{"VO", "2"},
                                          })}),
                    std::invalid_argument);

    CHECK_THROWS_AS(part(three_pin_map(), std::vector{symbol(std::vector{
                                              volt::PartSymbolPin{"GND", "1"},
                                              volt::PartSymbolPin{"VO", "2"},
                                              volt::PartSymbolPin{"VO", "2"},
                                              volt::PartSymbolPin{"VI", "3"},
                                          })}),
                    std::invalid_argument);
}

TEST_CASE("Part definition assembly rejects pin-pad mappings to unknown pins or pads") {
    CHECK_THROWS_AS(part(three_pin_map(), std::vector{symbol()},
                         orderable(std::vector{
                             volt::OrderablePinPadMapping{"1", "1"},
                             volt::OrderablePinPadMapping{"2", "2"},
                             volt::OrderablePinPadMapping{"9", "3"},
                         })),
                    std::invalid_argument);

    CHECK_THROWS_AS(part(three_pin_map(), std::vector{symbol()},
                         orderable(std::vector{
                             volt::OrderablePinPadMapping{"1", "1"},
                             volt::OrderablePinPadMapping{"2", "2"},
                             volt::OrderablePinPadMapping{"3", "99"},
                         })),
                    std::invalid_argument);
}

TEST_CASE("Part definition assembly requires multi-pad pins to use explicit pad mappings") {
    const auto collapsed_pad_label = std::vector{
        volt::PartFootprintPad{"1", -1.0, 0.0, 0.6, 0.6},
        volt::PartFootprintPad{"2,4", 0.0, 0.0, 0.6, 0.6},
        volt::PartFootprintPad{"3", 1.0, 0.0, 0.6, 0.6},
    };

    CHECK_THROWS_AS(part(three_pin_map(), std::vector{symbol()},
                         orderable(
                             std::vector{
                                 volt::OrderablePinPadMapping{"1", "1"},
                                 volt::OrderablePinPadMapping{"2", "2,4"},
                                 volt::OrderablePinPadMapping{"3", "3"},
                             },
                             collapsed_pad_label)),
                    std::invalid_argument);

    const auto accepted = part(three_pin_map(), std::vector{symbol()},
                               orderable(
                                   std::vector{
                                       volt::OrderablePinPadMapping{"1", "1"},
                                       volt::OrderablePinPadMapping{"2", "2"},
                                       volt::OrderablePinPadMapping{"2", "4"},
                                       volt::OrderablePinPadMapping{"3", "3"},
                                   },
                                   std::vector{
                                       volt::PartFootprintPad{"1", -1.0, 0.0, 0.6, 0.6},
                                       volt::PartFootprintPad{"2", 0.0, 0.0, 0.6, 0.6},
                                       volt::PartFootprintPad{"3", 1.0, 0.0, 0.6, 0.6},
                                       volt::PartFootprintPad{"4", 0.0, 1.0, 1.0, 1.0},
                                   }));

    CHECK(accepted.orderable_part().pin_pad_mappings().size() == 4U);
}

TEST_CASE("Part definition assembly resolves repeated pin names by distinct pin numbers") {
    const auto repeated_pins = std::vector{pin("VDD", "19"), pin("VDD", "32"), pin("VSS", "18")};
    const auto accepted_symbol =
        volt::HashedSchematicSymbolReference{"volt.test:mcu_power", "default", hash('a'),
                                             std::vector{
                                                 volt::PartSymbolPin{"VDD", "19"},
                                                 volt::PartSymbolPin{"VDD", "32"},
                                                 volt::PartSymbolPin{"VSS", "18"},
                                             }};
    const auto accepted_orderable = orderable(
        std::vector{
            volt::OrderablePinPadMapping{"19", "19"},
            volt::OrderablePinPadMapping{"32", "32"},
            volt::OrderablePinPadMapping{"18", "18"},
        },
        std::vector{
            volt::PartFootprintPad{"18", -1.0, 0.0, 0.6, 0.6},
            volt::PartFootprintPad{"19", 0.0, 0.0, 0.6, 0.6},
            volt::PartFootprintPad{"32", 1.0, 0.0, 0.6, 0.6},
        });

    CHECK_NOTHROW(part(repeated_pins, std::vector{accepted_symbol}, accepted_orderable));

    const auto duplicate_number_symbol =
        volt::HashedSchematicSymbolReference{"volt.test:mcu_power", "default", hash('a'),
                                             std::vector{
                                                 volt::PartSymbolPin{"VDD", "19"},
                                                 volt::PartSymbolPin{"VDD", "19"},
                                                 volt::PartSymbolPin{"VSS", "18"},
                                             }};
    auto repeated_orderable = orderable(
        std::vector{
            volt::OrderablePinPadMapping{"19", "19"},
            volt::OrderablePinPadMapping{"32", "32"},
            volt::OrderablePinPadMapping{"18", "18"},
        },
        std::vector{
            volt::PartFootprintPad{"18", -1.0, 0.0, 0.6, 0.6},
            volt::PartFootprintPad{"19", 0.0, 0.0, 0.6, 0.6},
            volt::PartFootprintPad{"32", 1.0, 0.0, 0.6, 0.6},
        });

    CHECK_THROWS_AS(part(repeated_pins, std::vector{duplicate_number_symbol}, repeated_orderable),
                    std::invalid_argument);
}

TEST_CASE("Part lineup diagnostics report unmapped pins with stable code severity and identity") {
    const auto report = volt::validate_part_lineup(part(three_pin_map(), std::vector{symbol()},
                                                        orderable(std::vector{
                                                            volt::OrderablePinPadMapping{"1", "1"},
                                                            volt::OrderablePinPadMapping{"3", "3"},
                                                        })));

    const auto *diagnostic = find_diagnostic(report, "PART_PIN_WITHOUT_PAD");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity() == volt::Severity::Warning);
    CHECK(diagnostic->category() == volt::DiagnosticCategory{"part.lineup"});
    CHECK(diagnostic->message().find("volt.power:AP1117-15@1.0.0") != std::string::npos);
}

TEST_CASE("Part lineup diagnostics report unmapped electrical pads") {
    const auto report = volt::validate_part_lineup(
        part(three_pin_map(), std::vector{symbol()},
             orderable(three_mappings(), std::vector{
                                             volt::PartFootprintPad{"1", -1.0, 0.0, 0.6, 0.6},
                                             volt::PartFootprintPad{"2", 0.0, 0.0, 0.6, 0.6},
                                             volt::PartFootprintPad{"3", 1.0, 0.0, 0.6, 0.6},
                                             volt::PartFootprintPad{"4", 2.0, 0.0, 0.6, 0.6},
                                         })));

    const auto *diagnostic = find_diagnostic(report, "PART_PAD_WITHOUT_PIN");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity() == volt::Severity::Warning);
}

TEST_CASE("Part lineup diagnostics report undeclared pad overlaps") {
    const auto report = volt::validate_part_lineup(
        part(three_pin_map(), std::vector{symbol()},
             orderable(three_mappings(), std::vector{
                                             volt::PartFootprintPad{"1", -0.2, 0.0, 0.6, 0.6},
                                             volt::PartFootprintPad{"2", 0.2, 0.0, 0.6, 0.6},
                                             volt::PartFootprintPad{"3", 1.4, 0.0, 0.6, 0.6},
                                         })));

    const auto *diagnostic = find_diagnostic(report, "PART_PAD_OVERLAP");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity() == volt::Severity::Warning);
}

TEST_CASE("Part lineup diagnostics report inconsistent pad pitch within a row") {
    const auto report = volt::validate_part_lineup(
        part(three_pin_map(), std::vector{symbol()},
             orderable(three_mappings(), std::vector{
                                             volt::PartFootprintPad{"1", 0.0, 0.0, 0.6, 0.6},
                                             volt::PartFootprintPad{"2", 1.0, 0.0, 0.6, 0.6},
                                             volt::PartFootprintPad{"3", 2.5, 0.0, 0.6, 0.6},
                                         })));

    const auto *diagnostic = find_diagnostic(report, "PART_PAD_ROW_PITCH_INCONSISTENT");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->severity() == volt::Severity::Warning);
}

TEST_CASE("Correct part produces zero lineup diagnostics") {
    CHECK(volt::validate_part_lineup(part()).empty());
}
