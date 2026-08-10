#include "publishing_core.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Require(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << message << std::endl;
		std::exit(1);
	}
}
}

int main()
{
	pdw::publishing::PublishEvent source;
	source.id = "event-1";
	source.timestamp = "2026-08-10T12:00:00+09:30";
	source.source = "PDW";
	source.address = "1234567";
	source.addressName = "Station Alpha";
	source.agency = "Test Agency";
	source.aliasColor = 12345;
	source.mode = "POCSAG";
	source.messageType = "Alpha";
	source.message = "A <test> & \"quote\"\nnext";
	source.filterMatched = true;
	source.filtered = true;
	source.groupCall = true;
	source.groupFinal = false;
	source.groupBit = 3;
	source.cycle = 2;
	source.frame = 17;
	pdw::publishing::TransformOptions options;
	options.sourceAlias = "Adelaide receiver";
	options.maskAddress = true;
	pdw::publishing::PublishEvent published = pdw::publishing::ApplyTransform(source, options);
	Require(source.address == "1234567", "transform mutated the decoded source event");
	Require(published.address == "****567", "address masking did not retain only the last three digits");
	Require(published.addressName.empty() && published.agency.empty() && published.aliasColor == 0,
		"address masking did not remove identifying capcode alias metadata");
	Require(published.source == "Adelaide receiver", "source alias was not applied to the published copy");

	std::string json = pdw::publishing::BuildJsonObject(published);
	Require(json.find("\\\"quote\\\"") != std::string::npos, "JSON quotes were not escaped");
	Require(json.find("\\nnext") != std::string::npos, "JSON newlines were not escaped");
	Require(json.find("\"filter_matched\":true") != std::string::npos,
		"filter match metadata is missing");
	Require(json.find("\"group_bit\":3") != std::string::npos,
		"FLEX group metadata is missing");
	Require(json.find("\"cycle\":2,\"frame\":17") != std::string::npos,
		"FLEX cycle/frame metadata is missing");
	options.maskAddress = false;
	const std::string aliasJson = pdw::publishing::BuildJsonObject(
		pdw::publishing::ApplyTransform(source, options), true);
	Require(aliasJson.find("\"address_name\":\"Station Alpha\"") != std::string::npos &&
		aliasJson.find("\"agency\":\"Test Agency\"") != std::string::npos,
		"capcode alias metadata is missing from the explicit local representation");
	Require(pdw::publishing::BuildJsonObject(source).find("address_name") == std::string::npos,
		"local alias metadata leaked into the established external JSON schema");
	std::vector<pdw::publishing::PublishEvent> events(1, published);
	Require(pdw::publishing::BuildRssFeed(events).find("&lt;test&gt; &amp;") != std::string::npos,
		"RSS text was not XML escaped");
	Require(pdw::publishing::BuildAtomFeed(events).find("urn:pdw:event-1") != std::string::npos,
		"Atom event identity is missing");
	Require(pdw::publishing::BuildHtmlFeed(events).find("<meta name=\"viewport\"") != std::string::npos,
		"HTML output is not mobile aware");
	Require(pdw::publishing::BuildJsonLines(events).find("\n") != std::string::npos,
		"JSONL output is not line delimited");
	return 0;
}
