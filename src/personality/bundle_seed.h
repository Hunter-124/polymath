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

// Provision/refresh the shipped starter bundles in `dest`:
//   * a bundle missing in `dest` is copied wholesale (first run / new persona);
//   * a bundle the user has NOT edited since we last wrote it has its
//     persona.json refreshed when the shipped stock changed (so improvements to
//     the stock personas reach already-seeded installs), plus any newly shipped
//     sibling files;
//   * a bundle the user HAS edited (or that predates our bookkeeping) is left
//     untouched, with the new stock dropped beside it as `persona.json.new` so
//     the update is discoverable without clobbering manual edits.
// Bookkeeping lives in a hidden `<dest>/.stock-manifest.json` mapping each
// persona name to the SHA-256 of the stock persona.json we last installed.
// Returns the number of bundles written (copied + refreshed). Exposed (rather
// than only the locate-then-seed wrapper) so it can be unit-tested with an
// explicit source tree.
int seedBundlesFrom(const std::filesystem::path& src,
                    const std::filesystem::path& dest);

// Locates the shipped bundles (locateStarterBundles()) and seeds/refreshes them
// into `dest` via seedBundlesFrom(). Returns the number of bundles written, or 0
// if no source could be found.
int seedStarterBundles(const std::filesystem::path& dest);

} // namespace polymath
