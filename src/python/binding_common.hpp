#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <volt/authoring/component_library.hpp>
#include <volt/authoring/reference_designators.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/properties.hpp>
#include <volt/io/logical_circuit_writer.hpp>
#include <volt/io/schematic_reader.hpp>
#include <volt/io/schematic_svg_writer.hpp>
#include <volt/io/schematic_writer.hpp>
#include <volt/schematic/default_symbols.hpp>
#include <volt/schematic/layout.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/schematic_document.hpp>
#include <volt/schematic/symbols.hpp>
#include <volt/schematic/validation.hpp>

namespace py = pybind11;
