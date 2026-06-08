#pragma once
//
// bundle_seed — first-run provisioning of starter persona bundles.
//
// The app ships a set of starter persona bundles under the source tree's
// assets/personalities/.  On first run (when data/personalities/ is empty) we
// copy them into the user's data dir so they can be switched, edited, or
// removed without touching the install.  Locating the bundled assets is
// deployment-dependent, so the resolver probes a list of candidate roots
// (compile-time source path first, then runtime-relative fallbacks).
//
#include <filesystem>

namespace polymath {

// Returns the directory that holds the shipped starter bundles, or an empty
// path if none of the candidate locations exist.  Pure lookup; no copying.
std::filesystem::path locateStarterBundles();

// If `dest` contains no persona bundles yet, copy every starter bundle found by
// locateStarterBundles() into it.  Returns the number of bundles copied (0 if
// the destination was already populated or no source could be found).
int seedStarterBundles(const std::filesystem::path& dest);

} // namespace polymath
