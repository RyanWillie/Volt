#pragma once

#include <memory>

#include "project_bundle_source.hpp"

namespace volt::io::detail {

class ProjectBundleStorage;

[[nodiscard]] std::shared_ptr<const ProjectBundleStorage>
open_project_bundle_v2(const BundleSource &source, CapturedEntry manifest_capture);

} // namespace volt::io::detail
