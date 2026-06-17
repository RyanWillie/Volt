#include <catch2/catch_test_macros.hpp>

#include <string>

#include <volt/io/schematic/schematic_schema.hpp>

TEST_CASE("Schematic schema exposes canonical text alignment strings") {
    CHECK(std::string{volt::io::text_horizontal_alignment_name(
              volt::TextHorizontalAlignment::Start)} == "Start");
    CHECK(std::string{volt::io::text_horizontal_alignment_name(
              volt::TextHorizontalAlignment::Middle)} == "Middle");
    CHECK(std::string{volt::io::text_horizontal_alignment_name(
              volt::TextHorizontalAlignment::End)} == "End");
    CHECK(volt::io::text_horizontal_alignment_from_name("Start") ==
          volt::TextHorizontalAlignment::Start);
    CHECK(volt::io::text_horizontal_alignment_from_name("Middle") ==
          volt::TextHorizontalAlignment::Middle);
    CHECK(volt::io::text_horizontal_alignment_from_name("End") ==
          volt::TextHorizontalAlignment::End);
    CHECK_FALSE(volt::io::text_horizontal_alignment_from_name("Centerish").has_value());

    CHECK(std::string{volt::io::text_vertical_alignment_name(volt::TextVerticalAlignment::Top)} ==
          "Top");
    CHECK(std::string{volt::io::text_vertical_alignment_name(
              volt::TextVerticalAlignment::Middle)} == "Middle");
    CHECK(std::string{volt::io::text_vertical_alignment_name(
              volt::TextVerticalAlignment::Bottom)} == "Bottom");
    CHECK(std::string{volt::io::text_vertical_alignment_name(
              volt::TextVerticalAlignment::Baseline)} == "Baseline");
    CHECK(volt::io::text_vertical_alignment_from_name("Top") == volt::TextVerticalAlignment::Top);
    CHECK(volt::io::text_vertical_alignment_from_name("Middle") ==
          volt::TextVerticalAlignment::Middle);
    CHECK(volt::io::text_vertical_alignment_from_name("Bottom") ==
          volt::TextVerticalAlignment::Bottom);
    CHECK(volt::io::text_vertical_alignment_from_name("Baseline") ==
          volt::TextVerticalAlignment::Baseline);
    CHECK_FALSE(volt::io::text_vertical_alignment_from_name("Centerish").has_value());
}

TEST_CASE("Schematic schema exposes related enum strings") {
    CHECK(std::string{volt::io::schematic_orientation_name(volt::SchematicOrientation::Right)} ==
          "Right");
    CHECK(volt::io::schematic_orientation_from_name("Left") == volt::SchematicOrientation::Left);
    CHECK_FALSE(volt::io::schematic_orientation_from_name("Diagonal").has_value());

    CHECK(std::string{volt::io::symbol_line_role_name(volt::SymbolLineRole::Normal)} == "Normal");
    CHECK(std::string{volt::io::symbol_line_role_name(volt::SymbolLineRole::TerminalLeadStart)} ==
          "TerminalLeadStart");
    CHECK(volt::io::symbol_line_role_from_name("TerminalLeadEnd") ==
          volt::SymbolLineRole::TerminalLeadEnd);
    CHECK_FALSE(volt::io::symbol_line_role_from_name("TerminalLeadMiddle").has_value());

    CHECK(std::string{volt::io::sheet_orientation_name(volt::SheetOrientation::Landscape)} ==
          "Landscape");
    CHECK(volt::io::sheet_orientation_from_name("Portrait") == volt::SheetOrientation::Portrait);
    CHECK_FALSE(volt::io::sheet_orientation_from_name("Square").has_value());

    CHECK(std::string{volt::io::route_intent_name(volt::RouteIntent::Direct)} == "Direct");
    CHECK(volt::io::route_intent_from_name("Orthogonal") == volt::RouteIntent::Orthogonal);
    CHECK_FALSE(volt::io::route_intent_from_name("Spline").has_value());

    CHECK(std::string{volt::io::power_port_kind_name(volt::PowerPortKind::Power)} == "Power");
    CHECK(volt::io::power_port_kind_from_name("Ground") == volt::PowerPortKind::Ground);
    CHECK_FALSE(volt::io::power_port_kind_from_name("Signal").has_value());

    CHECK(std::string{volt::io::sheet_port_kind_name(volt::SheetPortKind::Input)} == "Input");
    CHECK(std::string{volt::io::sheet_port_kind_name(volt::SheetPortKind::Output)} == "Output");
    CHECK(std::string{volt::io::sheet_port_kind_name(volt::SheetPortKind::Bidirectional)} ==
          "Bidirectional");
    CHECK(std::string{volt::io::sheet_port_kind_name(volt::SheetPortKind::OffPage)} == "OffPage");
    CHECK(volt::io::sheet_port_kind_from_name("OffPage") == volt::SheetPortKind::OffPage);
    CHECK_FALSE(volt::io::sheet_port_kind_from_name("Passive").has_value());
}
