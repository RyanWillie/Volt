#pragma once

#include <cctype>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>

#include <volt/core/errors.hpp>
#include <volt/core/ids.hpp>

namespace volt::io::detail {

/// @cond
template <typename Id> struct LocalIdPrefix;

template <> struct LocalIdPrefix<ComponentDefId> {
    static constexpr auto value = std::string_view{"component_def:"};
};

template <> struct LocalIdPrefix<ComponentId> {
    static constexpr auto value = std::string_view{"component:"};
};

template <> struct LocalIdPrefix<PinDefId> {
    static constexpr auto value = std::string_view{"pin_def:"};
};

template <> struct LocalIdPrefix<PinId> {
    static constexpr auto value = std::string_view{"pin:"};
};

template <> struct LocalIdPrefix<NetId> {
    static constexpr auto value = std::string_view{"net:"};
};

template <> struct LocalIdPrefix<ModuleDefId> {
    static constexpr auto value = std::string_view{"module_def:"};
};

template <> struct LocalIdPrefix<ModuleInstanceId> {
    static constexpr auto value = std::string_view{"module:"};
};

template <> struct LocalIdPrefix<TemplateNetDefId> {
    static constexpr auto value = std::string_view{"template_net:"};
};

template <> struct LocalIdPrefix<ModuleComponentId> {
    static constexpr auto value = std::string_view{"module_component:"};
};

template <> struct LocalIdPrefix<PortDefId> {
    static constexpr auto value = std::string_view{"port:"};
};

template <> struct LocalIdPrefix<NetClassId> {
    static constexpr auto value = std::string_view{"net_class:"};
};

template <> struct LocalIdPrefix<SymbolDefId> {
    static constexpr auto value = std::string_view{"symbol_def:"};
};

template <> struct LocalIdPrefix<SheetId> {
    static constexpr auto value = std::string_view{"sheet:"};
};

template <> struct LocalIdPrefix<SymbolInstanceId> {
    static constexpr auto value = std::string_view{"symbol_instance:"};
};

template <> struct LocalIdPrefix<WireRunId> {
    static constexpr auto value = std::string_view{"wire_run:"};
};

template <> struct LocalIdPrefix<NetLabelId> {
    static constexpr auto value = std::string_view{"net_label:"};
};

template <> struct LocalIdPrefix<JunctionId> {
    static constexpr auto value = std::string_view{"junction:"};
};

template <> struct LocalIdPrefix<PowerPortId> {
    static constexpr auto value = std::string_view{"power_port:"};
};

template <> struct LocalIdPrefix<NoConnectMarkerId> {
    static constexpr auto value = std::string_view{"no_connect_marker:"};
};

template <> struct LocalIdPrefix<SheetPortId> {
    static constexpr auto value = std::string_view{"sheet_port:"};
};

template <> struct LocalIdPrefix<SymbolFieldId> {
    static constexpr auto value = std::string_view{"symbol_field:"};
};

template <> struct LocalIdPrefix<BoardLayerId> {
    static constexpr auto value = std::string_view{"board_layer:"};
};

template <> struct LocalIdPrefix<BoardFeatureId> {
    static constexpr auto value = std::string_view{"board_feature:"};
};

template <> struct LocalIdPrefix<BoardTrackId> {
    static constexpr auto value = std::string_view{"board_track:"};
};

template <> struct LocalIdPrefix<BoardViaId> {
    static constexpr auto value = std::string_view{"board_via:"};
};

template <> struct LocalIdPrefix<BoardZoneId> {
    static constexpr auto value = std::string_view{"board_zone:"};
};

template <> struct LocalIdPrefix<BoardKeepoutId> {
    static constexpr auto value = std::string_view{"board_keepout:"};
};

template <> struct LocalIdPrefix<BoardRoomId> {
    static constexpr auto value = std::string_view{"board_room:"};
};

template <> struct LocalIdPrefix<BoardTextId> {
    static constexpr auto value = std::string_view{"board_text:"};
};

template <> struct LocalIdPrefix<FootprintDefId> {
    static constexpr auto value = std::string_view{"footprint_def:"};
};

template <> struct LocalIdPrefix<FootprintPadId> {
    static constexpr auto value = std::string_view{"footprint_pad:"};
};

template <> struct LocalIdPrefix<FootprintMarkingId> {
    static constexpr auto value = std::string_view{"footprint_marking:"};
};

template <> struct LocalIdPrefix<ComponentPlacementId> {
    static constexpr auto value = std::string_view{"component_placement:"};
};

/// @endcond

/** Return the canonical serialized local-ID prefix for a Volt entity ID type. */
template <typename Id> [[nodiscard]] constexpr std::string_view local_id_prefix() noexcept {
    return LocalIdPrefix<Id>::value;
}

/** Encode a Volt entity ID using the canonical local prefix plus its storage index. */
template <typename Id> [[nodiscard]] inline std::string encode_local_id(Id id) {
    auto encoded = std::string{local_id_prefix<Id>()};
    encoded += std::to_string(id.index());
    return encoded;
}

/** Decode and validate the numeric index suffix from a prefixed local ID string. */
[[nodiscard]] inline std::size_t decode_local_id_index(std::string_view id,
                                                       std::string_view prefix) {
    if (id.rfind(prefix, 0) != 0) {
        throw KernelLogicError{ErrorCode::InvalidArgument, "Local ID has the wrong typed prefix"};
    }
    const auto suffix = id.substr(prefix.size());
    if (suffix.empty()) {
        throw KernelLogicError{ErrorCode::InvalidArgument, "Local ID must contain an index"};
    }
    auto index = std::size_t{0};
    for (const auto character : suffix) {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
            throw KernelLogicError{ErrorCode::InvalidArgument, "Local ID index must be numeric"};
        }
        const auto digit = static_cast<std::size_t>(character - '0');
        if (index > (std::numeric_limits<std::size_t>::max() - digit) / std::size_t{10}) {
            throw KernelLogicError{ErrorCode::InvalidArgument, "Local ID index is too large"};
        }
        index = (index * std::size_t{10}) + digit;
    }
    return index;
}

/** Decode a canonical typed local ID string into its Volt entity ID type. */
template <typename Id> [[nodiscard]] inline Id decode_local_id(std::string_view id) {
    return Id{decode_local_id_index(id, local_id_prefix<Id>())};
}

} // namespace volt::io::detail
