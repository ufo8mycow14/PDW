#ifndef PDW_RECEIVER_CATALOG_H
#define PDW_RECEIVER_CATALOG_H

#ifndef STRICT
#define STRICT 1
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace pdw
{
namespace signal
{

struct ReceiverPackage
{
	std::string id;
	std::string displayName;
	std::string description;
	std::string libraryPath;
	bool bundled;
	bool compatible;
	std::string status;

	ReceiverPackage();
};

struct RtlSdrDeviceInfo
{
	unsigned int index;
	std::string name;
	std::string manufacturer;
	std::string product;
	std::string serial;

	RtlSdrDeviceInfo();
};

// Receiver packages live below Receivers beside the PDW executable. Keeping the optional
// native files there prevents them from changing legacy application startup.
std::string GetReceiverRootDirectory();

std::vector<ReceiverPackage> EnumerateRtlReceiverPackages(
	const std::string& receiverRoot = std::string());

bool ResolveRtlReceiverLibrary(const std::string& receiverId,
	std::string& libraryPath,
	std::string& error,
	const std::string& receiverRoot = std::string());

bool ValidateRtlReceiverLibrary(const std::string& libraryPath,
	std::string& error);

std::vector<RtlSdrDeviceInfo> EnumerateRtlSdrDevices(
	const std::string& libraryPath,
	std::string& error);

// Imports the selected primary DLL and neighbouring DLL dependencies into a
// portable custom package. The library must match the running PDW architecture
// and implement the librtlsdr receive API.
bool ImportRtlReceiverPackage(const std::string& primaryLibraryPath,
	const std::string& requestedDisplayName,
	ReceiverPackage& importedPackage,
	std::string& error,
	const std::string& receiverRoot = std::string());

} // namespace signal
} // namespace pdw

#endif
