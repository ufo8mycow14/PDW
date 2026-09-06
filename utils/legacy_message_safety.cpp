#include "legacy_message_safety.h"

#include <algorithm>
#include <cstdio>

namespace pdw { namespace legacy {
namespace {
bool Digit(char ch) { return ch >= '0' && ch <= '9'; }
bool Append(std::string& target, const std::string& value, std::size_t limit)
{
    if (target.size() > limit || value.size() > limit - target.size()) return false;
    target += value;
    return true;
}
// Far above a normal maximum-length decoded message with its indentation,
// but bounded even if all metadata fields are unexpectedly maximum length.
const std::size_t LogLimit = 1024 * 1024;
const char* const LogOversize = "[Log entry exceeds formatting limit]";
}

const char* DuplicateStatus(bool group)
{
    return group ? "Blocked Duplicate GroupMessage" : "Blocked Duplicate Message";
}

std::uint32_t DuplicateChecksum(const std::string& message)
{
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < message.size(); ++i)
    {
        if (i < 10 && Digit(message[i]))
        {
            std::uint32_t number = 0;
            do { number = number * 10u + static_cast<unsigned>(message[i++] - '0'); }
            while (i < message.size() && i <= 10 && Digit(message[i]));
            sum += number;
            --i;
        }
        else
        {
            // Preserve Windows' legacy signed-char checksum, with defined
            // unsigned arithmetic for formerly overflowing numeric inputs.
            const int byte = static_cast<unsigned char>(message[i]);
            sum += static_cast<std::uint32_t>((byte < 128 ? byte : byte - 256) - 31);
        }
    }
    return (sum & 0xffffu) | (static_cast<std::uint32_t>(message.size()) << 16);
}

std::uint32_t DuplicateAddress(int function, const std::string& address)
{
    const std::string text = std::to_string(function) + address;
    std::size_t pos = 0;
    const bool negative = text[pos] == '-';
    if (negative || text[pos] == '+') ++pos;
    std::uint32_t value = 0;
    while (pos < text.size() && Digit(text[pos]))
        value = value * 10u + static_cast<unsigned>(text[pos++] - '0');
    return negative ? 0u - value : value;
}

bool DuplicateCache::Contains(std::uint32_t address, std::uint32_t checksum,
    std::uint32_t now, std::uint32_t duration) const
{
    for (std::size_t i = 0; i < count_; ++i)
        if (entries_[i].address == address && entries_[i].checksum == checksum &&
            entries_[i].time + duration > now) return true;
    return false;
}
void DuplicateCache::RemoveFirst()
{
    if (!count_) return;
    for (std::size_t i = 1; i < count_; ++i) entries_[i - 1] = entries_[i];
    entries_[--count_] = {};
}
void DuplicateCache::Append(std::uint32_t address, std::uint32_t checksum, std::uint32_t now)
{
    if (!address) return; // Zero remains the legacy non-cacheable address.
    if (count_ == Capacity) RemoveFirst();
    entries_[count_++] = {address, checksum, now};
}
void DuplicateCache::RejectLast(std::uint32_t checksum)
{
    if (count_ && entries_[count_ - 1].checksum == checksum) entries_[--count_] = {};
}
void DuplicateCache::Expire(std::uint32_t now, std::uint32_t duration)
{
    while (count_ && now > entries_[0].time + duration) RemoveFirst();
}

std::string LogLine(const MessageFields& fields, const std::string& columns,
    bool acars, bool mobitex, bool linefeed, bool filter, bool wideFlex,
    std::size_t& labelSpacing)
{
    std::string output;
    labelSpacing = 0;
    for (std::size_t col = 1; col <= 7; ++col)
    {
        if (col == 7)
        {
            labelSpacing = output.size();
            if (!Append(output, " ", LogLimit)) return LogOversize;
        }
        if (columns.find(static_cast<char>('0' + col)) == std::string::npos) continue;
        const bool mobitexText = mobitex && filter && !fields[8].empty();
        const std::string& value = fields[col == 7 && !acars && mobitexText ? 8 : col];
        const bool expand = col == 7 && (acars || (mobitex ? mobitexText : linefeed));
        const char marker = acars ? '\x17' : '\xbb';
        if (expand)
        {
            const std::string indent = "\n" + std::string(labelSpacing + 1, ' ');
            for (char ch : value)
                if (!Append(output, ch == marker ? indent : std::string(1, ch), LogLimit))
                    return LogOversize;
        }
        else if (!Append(output, value, LogLimit)) return LogOversize;
        if (col < 7 && !Append(output, " ", LogLimit)) return LogOversize;
        if (col == 1 && wideFlex && fields[1].size() == 7 && !Append(output, "  ", LogLimit))
            return LogOversize;
    }
    return output;
}
void AppendLogLabel(std::string& line, const std::string& label, bool newline, std::size_t spacing)
{
    if (!label.empty())
    {
        if (spacing >= LogLimit || !Append(line, newline ? "\n" + std::string(spacing + 1, ' ') : " ", LogLimit) ||
            !Append(line, label, LogLimit)) line = LogOversize;
    }
    line += '\n';
}

bool CommandLine(const std::string& executable, const std::string& arguments,
    const MessageFields& fields, const std::string& label, int cycle, int frame,
    bool mobitex, std::size_t capacity, std::string& output)
{
    output.clear();
    if (!capacity || executable.size() >= capacity) return false;
    const std::size_t limit = capacity - 1;
    std::string parameters;
    for (std::size_t i = 0; i < arguments.size();)
    {
        std::string value(1, arguments[i]);
        std::size_t consumed = 1;
        if (arguments[i] == '%' && i + 1 < arguments.size())
        {
            const char next = arguments[i + 1];
            // Retain the old numeric-placeholder recognition, including
            // unknown multi-digit tokens, without an unchecked atoi.
            unsigned arg = 0;
            for (std::size_t p = i + 1; p < arguments.size() && Digit(arguments[p]); ++p)
            { arg = arg * 10 + static_cast<unsigned>(arguments[p] - '0'); if (arg > 7) break; }
            if (arg > 0 && arg < 8)
            {
                value = fields[arg];
                if (mobitex && arg == 7)
                    for (char& ch : value) if (ch == '\'' || ch == '"') ch = ' ';
                consumed = 2;
            }
            else if (next == '8') { value = label; consumed = 2; }
            else if (next == 'c' || next == 'C' || next == 'r' || next == 'R')
            {
                char number[32];
                std::snprintf(number, sizeof(number), next == 'c' || next == 'C' ? "%02i" : "%03i",
                    next == 'c' || next == 'C' ? cycle : frame);
                value = number;
                consumed = 2;
            }
        }
        if (!Append(parameters, value, limit)) return false;
        i += consumed;
    }
    std::string candidate = executable;
    if (!parameters.empty() && (!Append(candidate, " ", limit) || !Append(candidate, parameters, limit))) return false;
    output.swap(candidate);
    return true;
}
} }
