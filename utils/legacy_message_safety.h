#ifndef PDW_LEGACY_MESSAGE_SAFETY_H
#define PDW_LEGACY_MESSAGE_SAFETY_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace pdw { namespace legacy {

// These helpers own the legacy output/cache boundaries and are shared by the
// window timer and decoder paths. No decoded content belongs in status text.
const char* DuplicateStatus(bool group);
std::uint32_t DuplicateChecksum(const std::string& message);
std::uint32_t DuplicateAddress(int function, const std::string& address);

class DuplicateCache
{
public:
    static const std::size_t Capacity = 1000;
    std::size_t Count() const { return count_; }
    bool Contains(std::uint32_t address, std::uint32_t checksum,
        std::uint32_t now, std::uint32_t duration) const;
    void Append(std::uint32_t address, std::uint32_t checksum, std::uint32_t now);
    void RejectLast(std::uint32_t checksum);
    void Expire(std::uint32_t now, std::uint32_t duration);
private:
    struct Entry { std::uint32_t address, checksum, time; };
    void RemoveFirst();
    std::array<Entry, Capacity> entries_{};
    std::size_t count_ = 0;
};

using MessageFields = std::array<std::string, 9>;
std::string LogLine(const MessageFields& fields, const std::string& columns,
    bool acars, bool mobitex, bool linefeed, bool filter, bool wideFlex,
    std::size_t& labelSpacing);
void AppendLogLabel(std::string& line, const std::string& label,
    bool newline, std::size_t spacing);
// Capacity includes the NUL required by CreateProcess. Failure clears output;
// a partially expanded or truncated command must never reach the process API.
bool CommandLine(const std::string& executable, const std::string& arguments,
    const MessageFields& fields, const std::string& label, int cycle, int frame,
    bool mobitex, std::size_t capacity, std::string& output);

} }
#endif
