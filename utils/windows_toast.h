#ifndef PDW_WINDOWS_TOAST_H
#define PDW_WINDOWS_TOAST_H

#include <string>

namespace pdw
{
namespace outputs
{

// Call Initialize, Show, and Shutdown on the same worker thread.
class WindowsToast
{
public:
	struct State; // opaque implementation state

	WindowsToast();
	~WindowsToast();

	bool Initialize(std::string& error);
	bool Show(const std::string& utf8Title, const std::string& utf8Body, std::string& error);
	void Shutdown();
	bool IsAvailable() const;

private:
	WindowsToast(const WindowsToast&);
	WindowsToast& operator=(const WindowsToast&);

	State* state_;
};

} // namespace outputs
} // namespace pdw

#endif
