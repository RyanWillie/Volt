if(NOT DEFINED VOLT_RUNTIME_TARGET_FILE)
    message(FATAL_ERROR "VOLT_RUNTIME_TARGET_FILE is required")
endif()

if(NOT DEFINED VOLT_RUNTIME_TARGET_DIR)
    message(FATAL_ERROR "VOLT_RUNTIME_TARGET_DIR is required")
endif()

file(
    GET_RUNTIME_DEPENDENCIES
    LIBRARIES
        "${VOLT_RUNTIME_TARGET_FILE}"
    DIRECTORIES
        ${VOLT_RUNTIME_SEARCH_DIRS}
    RESOLVED_DEPENDENCIES_VAR
        VOLT_RESOLVED_RUNTIME_DEPENDENCIES
    UNRESOLVED_DEPENDENCIES_VAR
        VOLT_UNRESOLVED_RUNTIME_DEPENDENCIES
    PRE_EXCLUDE_REGEXES
        "api-ms-.*"
        "ext-ms-.*"
        "[Vv][Cc][Rr][Uu][Nn][Tt][Ii][Mm][Ee].*\\.dll"
        "[Mm][Ss][Vv][Cc][Pp].*\\.dll"
    POST_EXCLUDE_REGEXES
        ".*[\\\\/]Windows[\\\\/]System32[\\\\/].*"
        ".*[\\\\/]Windows[\\\\/]SysWOW64[\\\\/].*"
)

if(VOLT_UNRESOLVED_RUNTIME_DEPENDENCIES)
    message(
        FATAL_ERROR
        "Unresolved Python extension runtime dependencies: ${VOLT_UNRESOLVED_RUNTIME_DEPENDENCIES}"
    )
endif()

foreach(dependency IN LISTS VOLT_RESOLVED_RUNTIME_DEPENDENCIES)
    file(COPY "${dependency}" DESTINATION "${VOLT_RUNTIME_TARGET_DIR}")
endforeach()
