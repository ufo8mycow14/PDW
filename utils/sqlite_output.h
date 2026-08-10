#ifndef PDW_SQLITE_OUTPUT_H
#define PDW_SQLITE_OUTPUT_H

#include <string>

#include "publishing_core.h"

struct sqlite3;
struct sqlite3_stmt;

namespace pdw
{
namespace outputs
{

class SqliteOutput
{
public:
	SqliteOutput();
	~SqliteOutput();

	bool Open(const std::string& utf8Path, const std::string& table, std::string& error);
	bool Write(const pdw::publishing::PublishEvent& event, std::string& error);
	void Close();
	bool IsOpen() const;

private:
	SqliteOutput(const SqliteOutput&);
	SqliteOutput& operator=(const SqliteOutput&);

	sqlite3* database_;
	sqlite3_stmt* insert_;
	std::string path_;
	std::string table_;
};

} // namespace outputs
} // namespace pdw

#endif
