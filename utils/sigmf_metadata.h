#ifndef PDW_SIGMF_METADATA_H
#define PDW_SIGMF_METADATA_H
#include <cstdint>
#include <cmath>
#include <locale>
#include <set>
#include <sstream>
#include <string>

namespace pdw { namespace signal {
// The publishing JSON reader is deliberately tied to its canonical field
// order. SigMF requires general whitespace/order and nested extension values.
class SigMfMetadataReader
{
public:
	explicit SigMfMetadataReader(const std::string& text) : text_(text) {}
	bool Read(std::uint32_t& rate)
	{
		if (text_.size() > 1024 * 1024 || !Object(0, 1) || !End() || !datatype_ || !rate_) return false;
		rate = rate_;
		return true;
	}
private:
	void Space() { while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\r' || text_[pos_] == '\n')) ++pos_; }
	bool End() { Space(); return pos_ == text_.size(); }
	bool Take(char ch) { Space(); if (pos_ == text_.size() || text_[pos_] != ch) return false; ++pos_; return true; }
	bool Hex(unsigned& code)
	{
		code = 0;
		for (int i = 0; i < 4; ++i)
		{
			if (pos_ == text_.size()) return false;
			const char ch = text_[pos_++];
			const int digit = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
			if (digit < 0) return false;
			code = code * 16 + static_cast<unsigned>(digit);
		}
		return true;
	}
	bool String(std::string& value)
	{
		value.clear();
		if (!Take('"')) return false;
		while (pos_ < text_.size())
		{
			char ch = text_[pos_++];
			if (ch == '"') return true;
			if (static_cast<unsigned char>(ch) < 32) return false;
			if (ch != '\\') { value += ch; continue; }
			if (pos_ == text_.size()) return false;
			ch = text_[pos_++];
			if (ch == '"' || ch == '\\' || ch == '/') value += ch;
			else if (ch == 'b') value += '\b';
			else if (ch == 'f') value += '\f';
			else if (ch == 'n') value += '\n';
			else if (ch == 'r') value += '\r';
			else if (ch == 't') value += '\t';
			else if (ch == 'u')
			{
				unsigned code = 0;
				if (!Hex(code)) return false;
				if (code >= 0xd800 && code <= 0xdbff)
				{
					if (text_.compare(pos_, 2, "\\u") != 0) return false;
					pos_ += 2;
					unsigned low = 0;
					if (!Hex(low) || low < 0xdc00 || low > 0xdfff) return false;
					code = 0x10000 + (code - 0xd800) * 1024 + low - 0xdc00;
				}
				else if (code >= 0xdc00 && code <= 0xdfff) return false;
				if (code < 0x80) value += static_cast<char>(code);
				else
				{
					if (code >= 0x10000) value += static_cast<char>(0xf0 | code >> 18);
					if (code >= 0x800) value += static_cast<char>((code >= 0x10000 ? 0x80 : 0xe0) | (code >> 12 & 0x3f));
					value += static_cast<char>((code >= 0x800 ? 0x80 : 0xc0) | (code >> 6 & 0x3f));
					value += static_cast<char>(0x80 | (code & 0x3f));
				}
			}
			else return false;
		}
		return false;
	}
	bool Digit() const { return pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9'; }
	bool Number(std::string& number)
	{
		Space(); const std::size_t start = pos_;
		if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
		if (!Digit()) return false;
		if (text_[pos_++] != '0') while (Digit()) ++pos_;
		if (pos_ < text_.size() && text_[pos_] == '.') { ++pos_; if (!Digit()) return false; while (Digit()) ++pos_; }
		if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E'))
		{
			++pos_; if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
			if (!Digit()) return false;
			while (Digit()) ++pos_;
		}
		number = text_.substr(start, pos_ - start); return true;
	}
	bool Value(unsigned depth)
	{
		Space(); if (depth > 32 || pos_ == text_.size()) return false;
		if (text_[pos_] == '{') return Object(depth, 0);
		if (Take('['))
		{
			if (Take(']')) return true;
			do { if (!Value(depth + 1)) return false; } while (Take(','));
			return Take(']');
		}
		std::string ignored;
		if (text_[pos_] == '"') return String(ignored);
		for (const char* literal : {"true", "false", "null"})
		{
			const std::string token(literal);
			if (text_.compare(pos_, token.size(), token) == 0) { pos_ += token.size(); return true; }
		}
		return Number(ignored);
	}
	bool Object(unsigned depth, unsigned context)
	{
		if (depth > 32 || !Take('{')) return false;
		if (Take('}')) return true;
		std::set<std::string> keys;
		do
		{
			std::string key, value;
			if (!String(key) || !keys.insert(key).second || !Take(':')) return false;
			if (context == 1 && key == "global") { if (!Object(depth + 1, 2)) return false; }
			else if (context == 2 && key == "core:datatype") { if (!String(value) || value != "rf32_le") return false; datatype_ = true; }
			else if (context == 2 && key == "core:sample_rate")
			{
				if (!Number(value)) return false;
				std::istringstream input(value); input.imbue(std::locale::classic());
				double number = 0; input >> number;
				if (!input || !std::isfinite(number) || number < 1 || number > 10000000 || std::floor(number) != number) return false;
				rate_ = static_cast<std::uint32_t>(number);
			}
			else if (!Value(depth + 1)) return false;
		} while (Take(','));
		return Take('}');
	}
	const std::string& text_;
	std::size_t pos_ = 0;
	bool datatype_ = false;
	std::uint32_t rate_ = 0;
};
} }
#endif
