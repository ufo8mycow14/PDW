#ifndef PDW_POCSAG_TEST_ENVIRONMENT_H
#define PDW_POCSAG_TEST_ENVIRONMENT_H

#include <string>
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
	const std::vector<CapturedPocsagMessage>& CapturedPocsagMessages();
	const std::vector<int>& PocsagBitErrorObservations();
	bool PocsagFixtureHadInvalidCodeword();
	bool PocsagFixtureChangedPolarity();
}

#endif
