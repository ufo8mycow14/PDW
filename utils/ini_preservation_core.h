#ifndef PDW_INI_PRESERVATION_CORE_H
#define PDW_INI_PRESERVATION_CORE_H

#include <string>

namespace pdw
{
namespace ini
{

// Merge the complete set of settings emitted by PDW into an existing INI.
// PDW-owned keys are authoritative; unknown keys, comments, and sections are
// retained so older hardware options and future extensions survive a save.
std::string MergeKnownSettings(const std::string& existing,
	const std::string& generated);

// Atomically replace path with the merged settings using a temporary file in
// the same directory. The original remains in place if any step fails.
bool WriteMergedSettingsFile(const std::string& path,
	const std::string& generated, std::string& error);

} // namespace ini
} // namespace pdw

#endif
