#include "receiver_catalog.h"

#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			std::exit(1);
		}
	}

	std::string Join(const std::string& left, const std::string& right)
	{
		return left + "\\" + right;
	}
}

int main(int argc, char** argv)
{
	Expect(argc == 2, "mock receiver DLL path argument");
	std::string error;
	Expect(pdw::signal::ValidateRtlReceiverLibrary(argv[1], error), "mock receiver validates");
	const std::vector<pdw::signal::RtlSdrDeviceInfo> devices =
		pdw::signal::EnumerateRtlSdrDevices(argv[1], error);
	Expect(devices.size() == 1, "mock receiver enumerates one device");
	Expect(devices[0].product == "Test", "device USB strings are returned");

	char temporary[MAX_PATH] = {0};
	Expect(GetTempPathA(MAX_PATH, temporary) != 0, "temporary directory available");
	char suffix[64];
	snprintf(suffix, sizeof(suffix), "PDWReceiverTests-%lu", static_cast<unsigned long>(GetCurrentProcessId()));
	const std::string receiverRoot = Join(temporary, suffix);
	Expect(CreateDirectoryA(receiverRoot.c_str(), NULL) != FALSE, "test receiver root created");

	pdw::signal::ReceiverPackage imported;
	Expect(pdw::signal::ImportRtlReceiverPackage(argv[1], "Test Receiver", imported,
		error, receiverRoot), "compatible receiver imports");
	Expect(imported.id == "custom-test-receiver", "stable imported receiver identifier");
	const std::vector<pdw::signal::ReceiverPackage> packages =
		pdw::signal::EnumerateRtlReceiverPackages(receiverRoot);
	Expect(packages.size() == 1, "imported receiver enumerates");
	Expect(packages[0].compatible, "imported receiver remains compatible");
	std::string resolved;
	Expect(pdw::signal::ResolveRtlReceiverLibrary(imported.id, resolved, error,
		receiverRoot), "imported receiver resolves");
	Expect(resolved == imported.libraryPath, "resolved library path is portable package path");

	const std::string packageDirectory = Join(Join(receiverRoot, "Custom"), "test-receiver");
	DeleteFileA(Join(packageDirectory, "PDWMockRtlSdr.dll").c_str());
	DeleteFileA(Join(packageDirectory, "receiver.ini").c_str());
	RemoveDirectoryA(packageDirectory.c_str());
	RemoveDirectoryA(Join(receiverRoot, "Custom").c_str());
	RemoveDirectoryA(receiverRoot.c_str());

	std::cout << "Receiver catalogue tests passed\n";
	return 0;
}
