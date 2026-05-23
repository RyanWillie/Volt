#pragma once

#include "binding_common.hpp"

namespace volt::python {

namespace {

[[nodiscard]] inline volt::PropertyMap properties_from_dict(const py::dict &dict) {
    auto properties = volt::PropertyMap{};

    for (const auto item : dict) {
        if (!py::isinstance<py::str>(item.first)) {
            throw std::invalid_argument{"Property keys must be strings"};
        }

        auto key = volt::PropertyKey{py::cast<std::string>(item.first)};
        const auto value = item.second;

        if (py::isinstance<py::bool_>(value)) {
            properties.set(std::move(key), volt::PropertyValue{py::cast<bool>(value)});
        } else if (py::isinstance<py::int_>(value)) {
            properties.set(std::move(key), volt::PropertyValue{static_cast<std::int64_t>(
                                               py::cast<long long>(value))});
        } else if (py::isinstance<py::float_>(value)) {
            const auto number = py::cast<double>(value);
            if (!std::isfinite(number)) {
                throw std::invalid_argument{"Property numbers must be finite"};
            }
            properties.set(std::move(key), volt::PropertyValue{number});
        } else if (py::isinstance<py::str>(value)) {
            properties.set(std::move(key), volt::PropertyValue{py::cast<std::string>(value)});
        } else {
            throw std::invalid_argument{
                "Property values must be strings, booleans, ints, or floats"};
        }
    }

    return properties;
}

} // namespace

} // namespace volt::python
