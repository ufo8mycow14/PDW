#include "windows_toast.h"

#include <iostream>

int main()
{
	// Do not display a notification during automated tests. Construction must be
	// side-effect free; initialization and the HKCU AUMID registration are lazy.
	pdw::outputs::WindowsToast toast;
	if (toast.IsAvailable())
	{
		std::cerr << "Windows toast initialized before it was explicitly requested.\n";
		return 1;
	}
	std::cout << "Windows toast lazy-initialization test passed.\n";
	return 0;
}
