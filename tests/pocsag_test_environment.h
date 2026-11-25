#ifndef PDW_POCSAG_TEST_ENVIRONMENT_H
#define PDW_POCSAG_TEST_ENVIRONMENT_H

#include <string>
#include <cstddef>
#include <vector>

namespace pdw_test
{
	struct CapturedPocsagMessage
	{
		std::string address;
		std::string mode;
		std::string messageType;
		std::string bitrate;
		std::string payload;
	};

	void ResetPocsagEnvironment();
	std::vector<unsigned char> SyntheticPocsagIq(const std::vector<int>& bits, unsigned baud, bool reversePolarity);
	void RunPocsagAudio(const std::vector<int>& bits, unsigned baud, unsigned rate,
		std::size_t callbackBytes, bool reversePolarity);
	const std::vector<CapturedPocsagMessage>& CapturedPocsagMessages();
	const std::vector<int>& PocsagBitErrorObservations();
	bool PocsagFixtureHadInvalidCodeword();
	bool PocsagFixtureChangedPolarity();
}

#endif
