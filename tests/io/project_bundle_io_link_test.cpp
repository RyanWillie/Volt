#include <type_traits>

#include <volt/io/project_bundle.hpp>

static_assert(!std::is_copy_constructible_v<volt::io::ProjectBundle>);
static_assert(std::is_move_constructible_v<volt::io::ProjectBundle>);
static_assert(!std::is_default_constructible_v<volt::io::ProjectBundleGraphV2View>);

int main() {
    using volt::io::BundleIntegrityStatus;
    using volt::io::ProjectBundleOpenError;
    using volt::io::ProjectBundleOpenErrorCode;
    using volt::io::ProjectBundleSchemaVersion;
    const auto error =
        ProjectBundleOpenError{ProjectBundleOpenErrorCode::MissingBundle, "link contract"};
    return static_cast<int>(ProjectBundleSchemaVersion::V1) == 1 &&
                   BundleIntegrityStatus::LegacyUnverified != BundleIntegrityStatus::VerifiedV2 &&
                   error.code() == ProjectBundleOpenErrorCode::MissingBundle
               ? 0
               : 1;
}
