#include "publishing_core.h"

#include <sstream>

namespace pdw
{
namespace publishing
{
namespace
{
	std::string JsonEscape(const std::string& value)
	{
		std::ostringstream output;
		for (std::string::const_iterator character = value.begin(); character != value.end(); ++character)
		{
			const unsigned char valueByte = static_cast<unsigned char>(*character);
			switch (valueByte)
			{
				case '\"': output << "\\\""; break;
				case '\\': output << "\\\\"; break;
				case '\b': output << "\\b"; break;
				case '\f': output << "\\f"; break;
				case '\n': output << "\\n"; break;
				case '\r': output << "\\r"; break;
				case '\t': output << "\\t"; break;
				default:
					if (valueByte < 0x20)
					{
						static const char hex[] = "0123456789abcdef";
						output << "\\u00" << hex[valueByte >> 4] << hex[valueByte & 15];
					}
					else output << *character;
			}
		}
		return output.str();
	}

	std::string XmlEscape(const std::string& value)
	{
		std::string output;
		for (std::string::const_iterator character = value.begin(); character != value.end(); ++character)
		{
			switch (*character)
			{
				case '&': output += "&amp;"; break;
				case '<': output += "&lt;"; break;
				case '>': output += "&gt;"; break;
				case '\"': output += "&quot;"; break;
				case '\'': output += "&#39;"; break;
				default: output += *character;
			}
		}
		return output;
	}

	std::string EventTitle(const PublishEvent& event)
	{
		return event.mode + " " + event.messageType + " " + event.address;
	}
}

PublishEvent ApplyTransform(const PublishEvent& source, const TransformOptions& options)
{
	PublishEvent result(source);
	if (!options.sourceAlias.empty()) result.source = options.sourceAlias;
	if (options.maskAddress && !result.address.empty())
	{
		const std::size_t visible = result.address.size() > 3 ? 3 : 0;
		result.address.assign(result.address.size() - visible, '*');
		if (visible) result.address += source.address.substr(source.address.size() - visible);
		// A name or agency can identify the same pager as effectively as the raw
		// address. Address masking therefore removes directory metadata too.
		result.addressName.clear();
		result.agency.clear();
		result.aliasColor = 0;
	}
	if (!options.includeMessage) result.message.clear();
	return result;
}

std::string BuildJsonObject(const PublishEvent& event, bool includeLocalAliases)
{
	std::ostringstream output;
	output << "{\"id\":\"" << JsonEscape(event.id)
		<< "\",\"timestamp\":\"" << JsonEscape(event.timestamp)
		<< "\",\"source\":\"" << JsonEscape(event.source)
		<< "\",\"address\":\"" << JsonEscape(event.address) << '"';
	if (includeLocalAliases)
		output << ",\"address_name\":\"" << JsonEscape(event.addressName)
			<< "\",\"agency\":\"" << JsonEscape(event.agency)
			<< "\",\"alias_color\":" << event.aliasColor;
	output << ",\"time\":\"" << JsonEscape(event.time)
		<< "\",\"date\":\"" << JsonEscape(event.date)
		<< "\",\"mode\":\"" << JsonEscape(event.mode)
		<< "\",\"message_type\":\"" << JsonEscape(event.messageType)
		<< "\",\"bitrate\":\"" << JsonEscape(event.bitrate)
		<< "\",\"message\":\"" << JsonEscape(event.message)
		<< "\",\"filter_label\":\"" << JsonEscape(event.filterLabel)
		<< "\",\"filter_matched\":" << (event.filterMatched ? "true" : "false")
		<< ",\"monitor_only\":" << (event.monitorOnly ? "true" : "false")
		<< ",\"filtered\":" << (event.filtered ? "true" : "false")
		<< ",\"rejected\":" << (event.rejected ? "true" : "false")
		<< ",\"blocked_duplicate\":" << (event.blockedDuplicate ? "true" : "false")
		<< ",\"group_call\":" << (event.groupCall ? "true" : "false")
		<< ",\"group_final\":" << (event.groupFinal ? "true" : "false")
		<< ",\"fragmented\":" << (event.fragmented ? "true" : "false")
		<< ",\"assembled\":" << (event.assembled ? "true" : "false")
		<< ",\"filter_index\":";
	if (event.filterIndex >= 0) output << event.filterIndex; else output << "null";
	output << ",\"group_bit\":";
	if (event.groupBit >= 0) output << event.groupBit; else output << "null";
	output << ",\"cycle\":";
	if (event.cycle >= 0) output << event.cycle; else output << "null";
	output << ",\"frame\":";
	if (event.frame >= 0) output << event.frame; else output << "null";
	output << "}";
	return output.str();
}

std::string BuildJsonFeed(const std::vector<PublishEvent>& events)
{
	std::ostringstream output;
	output << "{\"version\":1,\"events\":[";
	for (std::size_t index = 0; index < events.size(); ++index)
	{
		if (index) output << ',';
		output << BuildJsonObject(events[index]);
	}
	output << "]}\n";
	return output.str();
}

std::string BuildJsonLines(const std::vector<PublishEvent>& events)
{
	std::ostringstream output;
	for (std::vector<PublishEvent>::const_iterator event = events.begin(); event != events.end(); ++event)
		output << BuildJsonObject(*event) << '\n';
	return output.str();
}

std::string BuildRssFeed(const std::vector<PublishEvent>& events)
{
	std::ostringstream output;
	output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rss version=\"2.0\"><channel>"
		<< "<title>PDW published messages</title><description>Operator-authorised PDW feed</description>";
	for (std::vector<PublishEvent>::const_iterator event = events.begin(); event != events.end(); ++event)
		output << "<item><guid isPermaLink=\"false\">" << XmlEscape(event->id)
			<< "</guid><title>" << XmlEscape(EventTitle(*event)) << "</title><pubDate>"
			<< XmlEscape(event->timestamp) << "</pubDate><description>"
			<< XmlEscape(event->message) << "</description></item>";
	output << "</channel></rss>\n";
	return output.str();
}

std::string BuildAtomFeed(const std::vector<PublishEvent>& events)
{
	std::ostringstream output;
	output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		<< "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>PDW published messages</title>";
	if (!events.empty()) output << "<updated>" << XmlEscape(events.front().timestamp) << "</updated>";
	for (std::vector<PublishEvent>::const_iterator event = events.begin(); event != events.end(); ++event)
		output << "<entry><id>urn:pdw:" << XmlEscape(event->id) << "</id><title>"
			<< XmlEscape(EventTitle(*event)) << "</title><updated>" << XmlEscape(event->timestamp)
			<< "</updated><content type=\"text\">" << XmlEscape(event->message)
			<< "</content></entry>";
	output << "</feed>\n";
	return output.str();
}

std::string BuildHtmlFeed(const std::vector<PublishEvent>& events)
{
	std::ostringstream output;
	output << "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
		<< "<title>PDW published messages</title><style>body{font-family:system-ui;margin:2rem;background:#f7f7f8;color:#18181b}"
		<< "table{width:100%;border-collapse:collapse;background:white}th,td{padding:.6rem;border:1px solid #ddd;text-align:left;vertical-align:top}"
		<< "th{background:#eee}@media(max-width:700px){table{font-size:.85rem}}</style></head><body><h1>PDW published messages</h1>"
		<< "<table><thead><tr><th>Time</th><th>Mode</th><th>Address</th><th>Type</th><th>Message</th></tr></thead><tbody>";
	for (std::vector<PublishEvent>::const_iterator event = events.begin(); event != events.end(); ++event)
		output << "<tr><td>" << XmlEscape(event->timestamp) << "</td><td>" << XmlEscape(event->mode)
			<< "</td><td>" << XmlEscape(event->address) << "</td><td>" << XmlEscape(event->messageType)
			<< "</td><td>" << XmlEscape(event->message) << "</td></tr>";
	output << "</tbody></table></body></html>\n";
	return output.str();
}

} // namespace publishing
} // namespace pdw
