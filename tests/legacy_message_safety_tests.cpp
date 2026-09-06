#include "legacy_message_safety.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void Expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}
}

int main()
{
    using namespace pdw::legacy;
    MessageFields fields;
    fields[1] = "1234567";
    fields[2] = "12:34";
    fields[7] = "first\xbbsecond";
    std::size_t spacing = 0;
    std::string line = LogLine(fields, "127", false, false, true, false, false, spacing);
    Expect(line == "1234567 12:34  first\n               second" && spacing == 14,
        "paging linefeeds preserve normal metadata and indentation");
    AppendLogLabel(line, "Synthetic label", true, spacing);
    Expect(line == "1234567 12:34  first\n               second\n               Synthetic label\n",
        "labels and final newline use the same dynamically sized output");
    Expect(LogLine(fields, "17", false, false, false, false, true, spacing) ==
        "1234567    first\xbbsecond", "disabled linefeed and wide FLEX spacing remain intact");
    fields[7] = "first\x17second";
    Expect(LogLine(fields, "7", true, false, false, false, false, spacing) == " first\n second",
        "ACARS ETB expands regardless of paging linefeed option");
    fields[7] = "fallback";
    fields[8] = "mobi\xbbtext";
    Expect(LogLine(fields, "7", false, true, false, true, false, spacing) == " mobi\n text",
        "filtered Mobitex uses expanded replacement text");
    Expect(LogLine(fields, "7", false, true, true, false, false, spacing) == " fallback",
        "monitor Mobitex retains its ordinary message field");
    for (int mode = 0; mode != 3; ++mode)
    {
        fields[7] = std::string(5119, mode == 0 ? '\x17' : '\xbb');
        fields[8] = fields[7];
        line = LogLine(fields, "127", mode == 0, mode == 1, true, true, false, spacing);
        Expect(line.size() == 15 + 5119 * 16, "maximum multiline input expands completely beyond former fixed buffer");
        AppendLogLabel(line, std::string(300, 'L'), true, spacing);
        Expect(line.size() == 15 + 5119 * 16 + 16 + 300 + 1,
            "large labels safely follow maximum expanded input");
    }
    fields[1] = std::string(5119, '1');
    fields[7] = std::string(5119, '\xbb');
    line = LogLine(fields, "17", false, false, true, false, false, spacing);
    Expect(line == "[Log entry exceeds formatting limit]", "pathological metadata expansion is bounded and explicit");

    Expect(DuplicateChecksum("123456789") == (9u << 16 | 52501u), "nine digit checksum preserves legacy value");
    Expect(DuplicateChecksum("1234567890") == (10u << 16 | 722u), "ten digit numeric input is bounded");
    Expect(DuplicateChecksum("12345678901") == (11u << 16 | 7221u), "eleven digit input uses defined low checksum bits");
    Expect(DuplicateChecksum("A12B") == (4u << 16 | 81u), "mixed numeric and text checksum retains legacy grouping");
    Expect(DuplicateChecksum(std::string(1, '\xff')) == (1u << 16 | 65504u),
        "high-bit bytes never enter locale-dependent digit classification");
    Expect(DuplicateAddress(3, "1234567") == 31234567u, "function and ordinary capcode remain distinct");
    Expect(DuplicateAddress(3, "999999999") == 3999999999u, "nine digit FLEX address fits without a temporary buffer");
    Expect(DuplicateAddress(0, "-123") == 0u, "signed synthetic group address retains legacy conversion behaviour");
    Expect(DuplicateAddress(0, std::string(5119, '9')) == 0xffffffffu, "very long addresses have defined unsigned conversion");
    struct GuardedCache { std::uint32_t before = 0x1234abcd; DuplicateCache cache; std::uint32_t after = 0xdcba4321; } guarded;
    DuplicateCache& cache = guarded.cache;
    cache.RejectLast(1); cache.Expire(100, 60);
    Expect(cache.Count() == 0, "empty expiry and rejection stay empty");
    cache.Append(1, 1, 100); cache.Append(2, 2, 101);
    Expect(cache.Count() == 2 && cache.Contains(1, 1, 101, 60), "second insertion retains the first entry");
    for (unsigned i = 3; i <= 999; ++i) cache.Append(i, i, 101);
    Expect(cache.Count() == 999, "cache enumerates 999 entries");
    cache.Append(1000, 1000, 101);
    Expect(cache.Count() == 1000 && cache.Contains(1000, 1000, 102, 60), "full cache bounds enumeration and final row lookup");
    cache.RejectLast(999);
    Expect(cache.Count() == 1000, "reject only removes matching last message");
    cache.RejectLast(1000);
    Expect(cache.Count() == 999, "reject can remove the final row at capacity");
    cache.Append(1000, 1000, 101); cache.Append(1001, 1001, 102);
    Expect(cache.Count() == 1000 && !cache.Contains(1, 1, 102, 60) && cache.Contains(2, 2, 102, 60),
        "full cache evicts exactly the oldest entry");
    cache.Expire(161, 60);
    Expect(cache.Count() == 1000 && !cache.Contains(2, 2, 161, 60), "legacy exact expiry boundary is preserved");
    cache.Expire(162, 60);
    Expect(cache.Count() == 1 && cache.Contains(1001, 1001, 162, 61), "consecutive expiry retains the newest entry");
    cache.Expire(163, 60);
    Expect(cache.Count() == 0, "expiry clears the tail and terminates");
    cache.Append(0, 1, 0);
    Expect(cache.Count() == 0, "zero address remains non-cacheable");
    cache.Append(1, 1, 100);
    cache.Expire(90, 60);
    Expect(cache.Contains(1, 1, 90, 60), "backwards wall-clock adjustment retains legacy expiry behaviour");
    Expect(guarded.before == 0x1234abcd && guarded.after == 0xdcba4321, "cache operations preserve adjacent sentinels");
    Expect(std::string(DuplicateStatus(false)) == "Blocked Duplicate Message" &&
        std::string(DuplicateStatus(true)) == "Blocked Duplicate GroupMessage", "both status paths are fixed content-free messages");

    fields = {};
    for (int i = 1; i <= 7; ++i) fields[i] = std::to_string(i);
    std::string command;
    Expect(CommandLine("tool", "%1 %2 %3 %4 %5 %6 %7 %8 %c %C %r %R", fields, "label", 2, 3, false, 5120, command) &&
        command == "tool 1 2 3 4 5 6 7 label 02 02 003 003", "every supported command placeholder preserves its value");
    Expect(CommandLine("tool", "%x %% %10 %", fields, "", 0, 0, false, 5120, command) &&
        command == "tool %x %% %10 %", "unknown and trailing placeholders remain literal");
    fields[7] = "a\"b'c";
    Expect(CommandLine("tool", "%7", fields, "", 0, 0, true, 5120, command) && command == "tool a b c",
        "Mobitex quote replacement is preserved");
    fields[7] = std::string(5114, 'X');
    Expect(CommandLine("tool", "%7", fields, "", 0, 0, false, 5120, command) && command.size() == 5119,
        "exact command limit includes executable separator and terminator");
    fields[7] += 'X';
    Expect(!CommandLine("tool", "%7", fields, "", 0, 0, false, 5120, command) && command.empty(),
        "one byte excess clears command rather than truncating execution");
    fields[7] = std::string(5119, 'X');
    Expect(!CommandLine("tool", "%7%7", fields, "", 0, 0, false, 5120, command) && command.empty(),
        "repeated maximum message placeholders cannot overflow or launch partial arguments");
    Expect(!CommandLine(std::string(5120, 'X'), "", fields, "", 0, 0, false, 5120, command),
        "oversized executable is rejected independently");
    Expect(!CommandLine("tool", "%8", fields, std::string(5119, 'L'), 0, 0, false, 5120, command),
        "label expansion uses the complete command limit");
    Expect(CommandLine("tool", "", fields, "", (std::numeric_limits<int>::min)(),
        (std::numeric_limits<int>::max)(), false, 5120, command) && command == "tool", "empty arguments remain valid");
    Expect(CommandLine("tool", "%c %r", fields, "", (std::numeric_limits<int>::min)(),
        (std::numeric_limits<int>::max)(), false, 5120, command) && command == "tool -2147483648 2147483647",
        "numeric command formatting accepts full int range without temporary overflow");
    std::cout << "Legacy message safety tests passed\n";
}
