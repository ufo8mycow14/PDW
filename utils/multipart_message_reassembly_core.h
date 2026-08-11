#ifndef PDW_MULTIPART_MESSAGE_REASSEMBLY_CORE_H
#define PDW_MULTIPART_MESSAGE_REASSEMBLY_CORE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pdw
{
namespace multipart
{

enum MultipartStatus
{
	MULTIPART_NOT_RECOGNIZED = 0,
	MULTIPART_BUFFERED,
	MULTIPART_ASSEMBLED,
	MULTIPART_DUPLICATE,
	MULTIPART_CONFLICT,
	MULTIPART_CAPACITY_REACHED,
	MULTIPART_INVALID
};

struct MultipartObservation
{
	std::string address;
	std::string protocol;
	std::string messageType;
	std::uint64_t observedAtMs;
	const unsigned char* text;
	const std::uint8_t* colors;
	std::size_t length;

	MultipartObservation();
};

struct MultipartResult
{
	MultipartStatus status;
	bool assembled;
	bool truncated;
	unsigned int partNumber;
	unsigned int totalParts;
	std::string text;
	std::vector<std::uint8_t> colors;

	MultipartResult();
};

// Reassembles explicit human-readable markers such as "Part 1 of 2". The
// marker supplies no independent message identifier, so at most one chain for
// an address/protocol/type/part-count identity is accepted at a time.
class MultipartReassembler
{
public:
	explicit MultipartReassembler(
		std::size_t maximumSlots = 64,
		std::uint64_t timeoutMs = 600000,
		std::size_t maximumBytes = 5119,
		unsigned int maximumParts = 32);

	MultipartResult Observe(const MultipartObservation& observation);
	void Reset();
	std::size_t ActiveCount() const;

private:
	struct Part
	{
		bool present;
		std::string text;
		std::vector<std::uint8_t> colors;

		Part();
	};

	struct Slot
	{
		bool active;
		std::string address;
		std::string protocol;
		std::string messageType;
		unsigned int totalParts;
		std::uint64_t observedAtMs;
		std::vector<Part> parts;

		Slot();
	};

	void Expire(std::uint64_t nowMs);
	std::size_t Find(const MultipartObservation& observation,
		unsigned int totalParts) const;
	std::size_t FindFree() const;
	void Clear(std::size_t index);
	MultipartResult Complete(std::size_t index);

	std::vector<Slot> slots_;
	std::uint64_t timeoutMs_;
	std::size_t maximumBytes_;
	unsigned int maximumParts_;
};

} // namespace multipart
} // namespace pdw

#endif
