include(GNUInstallDirs)

# Register one Volt library in the VoltTargets export set.
#
# `export_name` must match the in-tree `Volt::<export_name>` alias so that consumers see
# identical target names whether they use `find_package(Volt)` or `add_subdirectory`.
function(volt_install_library target export_name)
    if(NOT VOLT_INSTALL)
        return()
    endif()

    set_target_properties(${target} PROPERTIES EXPORT_NAME ${export_name})
    install(
        TARGETS ${target}
        EXPORT VoltTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endfunction()

# Install public headers plus the generated package configuration files.
function(volt_install_package)
    if(NOT VOLT_INSTALL)
        return()
    endif()

    install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/volt DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    set(VOLT_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/Volt)
    install(
        EXPORT VoltTargets
        FILE VoltTargets.cmake
        NAMESPACE Volt::
        DESTINATION ${VOLT_INSTALL_CMAKEDIR}
    )

    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        ${PROJECT_SOURCE_DIR}/cmake/VoltConfig.cmake.in
        ${PROJECT_BINARY_DIR}/VoltConfig.cmake
        INSTALL_DESTINATION ${VOLT_INSTALL_CMAKEDIR}
    )
    write_basic_package_version_file(
        ${PROJECT_BINARY_DIR}/VoltConfigVersion.cmake
        COMPATIBILITY SameMajorVersion
    )
    install(
        FILES ${PROJECT_BINARY_DIR}/VoltConfig.cmake
              ${PROJECT_BINARY_DIR}/VoltConfigVersion.cmake
        DESTINATION ${VOLT_INSTALL_CMAKEDIR}
    )
endfunction()
