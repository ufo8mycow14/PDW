#ifndef PDW_MYSQL_ODBC_OUTPUT_H
#define PDW_MYSQL_ODBC_OUTPUT_H

#include <string>

#include "publishing_core.h"

namespace pdw
{
namespace outputs
{

class MysqlOdbcOutput
{
public:
	MysqlOdbcOutput();
	~MysqlOdbcOutput();

	bool Open(const std::string& dsn, const std::string& username,
		const std::string& password, const std::string& table, std::string& error);
	bool Write(const pdw::publishing::PublishEvent& event, std::string& error);
	void Close();
	bool IsOpen() const;

private:
	MysqlOdbcOutput(const MysqlOdbcOutput&);
	MysqlOdbcOutput& operator=(const MysqlOdbcOutput&);

	void* environment_;
	void* connection_;
	std::string dsn_;
	std::string username_;
	std::string table_;
};

} // namespace outputs
} // namespace pdw

#endif
