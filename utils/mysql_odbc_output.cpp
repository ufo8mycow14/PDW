#include "mysql_odbc_output.h"

#include <windows.h>
#include <sql.h>
#include <sqlext.h>

#include <sstream>
#include <vector>

#include "data_output_core.h"

namespace pdw
{
namespace outputs
{
namespace
{
	bool Succeeded(SQLRETURN result)
	{
		return result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO;
	}

	std::string Diagnostics(SQLSMALLINT handleType, SQLHANDLE handle, const char* fallback)
	{
		std::ostringstream output;
		SQLCHAR state[7] = {};
		SQLCHAR message[512] = {};
		SQLINTEGER nativeError = 0;
		SQLSMALLINT messageLength = 0;
		for (SQLSMALLINT record = 1; ; ++record)
		{
			const SQLRETURN diagnostic = SQLGetDiagRecA(handleType, handle, record, state,
				&nativeError, message, sizeof(message), &messageLength);
			if (diagnostic == SQL_NO_DATA || !Succeeded(diagnostic)) break;
			if (record > 1) output << " | ";
			output << reinterpret_cast<const char*>(state) << ": "
				<< reinterpret_cast<const char*>(message);
		}
		const std::string result(output.str());
		return result.empty() ? fallback : result;
	}

	std::string EscapeConnectionValue(const std::string& value)
	{
		std::string result("{");
		for (std::string::const_iterator character = value.begin(); character != value.end(); ++character)
		{
			result += *character;
			if (*character == '}') result += '}';
		}
		result += '}';
		return result;
	}

	void HideSecret(std::string& message, const std::string& secret)
	{
		if (secret.empty()) return;
		std::size_t position = 0;
		while ((position = message.find(secret, position)) != std::string::npos)
		{
			message.replace(position, secret.size(), "********");
			position += 8;
		}
	}

	bool ExecuteDirect(SQLHDBC connection, const std::string& sql, std::string& error)
	{
		SQLHSTMT statement = SQL_NULL_HSTMT;
		if (!Succeeded(SQLAllocHandle(SQL_HANDLE_STMT, connection, &statement)))
		{
			error = "MySQL ODBC could not allocate a statement.";
			return false;
		}
		SQLSetStmtAttr(statement, SQL_ATTR_QUERY_TIMEOUT,
			reinterpret_cast<SQLPOINTER>(static_cast<ULONG_PTR>(10)), 0);
		const SQLRETURN result = SQLExecDirectA(statement,
			reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())), SQL_NTS);
		if (!Succeeded(result)) error = Diagnostics(SQL_HANDLE_STMT, statement, "MySQL ODBC statement failed.");
		SQLFreeHandle(SQL_HANDLE_STMT, statement);
		return Succeeded(result);
	}

	bool BindText(SQLHSTMT statement, SQLUSMALLINT index, std::string& value, SQLLEN& length,
		SQLULEN columnSize, SQLSMALLINT sqlType = SQL_VARCHAR)
	{
		length = static_cast<SQLLEN>(value.size());
		return Succeeded(SQLBindParameter(statement, index, SQL_PARAM_INPUT, SQL_C_CHAR,
			sqlType, columnSize, 0, const_cast<char*>(value.c_str()),
			static_cast<SQLLEN>(value.size() + 1), &length));
	}

	bool BindInt(SQLHSTMT statement, SQLUSMALLINT index, SQLINTEGER& value, SQLLEN& length)
	{
		length = 0;
		return Succeeded(SQLBindParameter(statement, index, SQL_PARAM_INPUT, SQL_C_SLONG,
			SQL_INTEGER, 0, 0, &value, 0, &length));
	}
}

MysqlOdbcOutput::MysqlOdbcOutput()
	: environment_(SQL_NULL_HENV), connection_(SQL_NULL_HDBC) {}

MysqlOdbcOutput::~MysqlOdbcOutput()
{
	Close();
}

bool MysqlOdbcOutput::Open(const std::string& dsn, const std::string& username,
	const std::string& password, const std::string& table, std::string& error)
{
	error.clear();
	if (dsn.empty())
	{
		error = "Choose a configured MySQL ODBC data source name (DSN).";
		return false;
	}
	if (!IsSafeSqlIdentifier(table))
	{
		error = "MySQL table name must begin with a letter or underscore and contain only letters, numbers, or underscores.";
		return false;
	}
	if (IsOpen() && dsn_ == dsn && username_ == username && table_ == table) return true;
	Close();

	SQLHENV environment = SQL_NULL_HENV;
	if (!Succeeded(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &environment)))
	{
		error = "MySQL ODBC could not allocate its environment.";
		return false;
	}
	environment_ = environment;
	if (!Succeeded(SQLSetEnvAttr(environment, SQL_ATTR_ODBC_VERSION,
		reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0)))
	{
		error = Diagnostics(SQL_HANDLE_ENV, environment, "MySQL ODBC could not select ODBC 3.");
		Close();
		return false;
	}
	SQLHDBC connection = SQL_NULL_HDBC;
	if (!Succeeded(SQLAllocHandle(SQL_HANDLE_DBC, environment, &connection)))
	{
		error = Diagnostics(SQL_HANDLE_ENV, environment, "MySQL ODBC could not allocate a connection.");
		Close();
		return false;
	}
	connection_ = connection;
	SQLSetConnectAttr(connection, SQL_LOGIN_TIMEOUT, reinterpret_cast<SQLPOINTER>(5), 0);
	SQLSetConnectAttr(connection, SQL_ATTR_CONNECTION_TIMEOUT, reinterpret_cast<SQLPOINTER>(10), 0);
	std::string connectionString = "DSN=" + EscapeConnectionValue(dsn) + ";";
	if (!username.empty()) connectionString += "UID=" + EscapeConnectionValue(username) + ";";
	if (!password.empty()) connectionString += "PWD=" + EscapeConnectionValue(password) + ";";
	SQLCHAR completed[1024] = {};
	SQLSMALLINT completedLength = 0;
	const SQLRETURN connected = SQLDriverConnectA(connection, NULL,
		reinterpret_cast<SQLCHAR*>(const_cast<char*>(connectionString.c_str())), SQL_NTS,
		completed, sizeof(completed), &completedLength, SQL_DRIVER_NOPROMPT);
	SecureZeroMemory(&completed[0], sizeof(completed));
	SecureZeroMemory(&connectionString[0], connectionString.size());
	if (!Succeeded(connected))
	{
		error = Diagnostics(SQL_HANDLE_DBC, connection, "MySQL ODBC connection failed.");
		HideSecret(error, password);
		Close();
		return false;
	}

	if (!ExecuteDirect(connection, "SET NAMES utf8mb4", error))
	{
		Close();
		return false;
	}
	const std::string quoted = "`" + table + "`";
	const std::string schema =
		"CREATE TABLE IF NOT EXISTS " + quoted + " ("
		"id VARCHAR(96) NOT NULL PRIMARY KEY,"
		"received_utc VARCHAR(40) NOT NULL,"
		"source VARCHAR(128) NOT NULL DEFAULT '',"
		"address VARCHAR(64) NOT NULL DEFAULT '',"
		"local_time VARCHAR(32) NOT NULL DEFAULT '',"
		"local_date VARCHAR(32) NOT NULL DEFAULT '',"
		"mode VARCHAR(64) NOT NULL DEFAULT '',"
		"message_type VARCHAR(64) NOT NULL DEFAULT '',"
		"bitrate VARCHAR(32) NOT NULL DEFAULT '',"
		"message MEDIUMTEXT NOT NULL,"
		"filter_label TEXT NOT NULL,"
		"filter_matched TINYINT NOT NULL DEFAULT 0,"
		"monitor_only TINYINT NOT NULL DEFAULT 0,"
		"filtered TINYINT NOT NULL DEFAULT 0,"
		"rejected TINYINT NOT NULL DEFAULT 0,"
		"blocked_duplicate TINYINT NOT NULL DEFAULT 0,"
		"group_call TINYINT NOT NULL DEFAULT 0,"
		"group_final TINYINT NOT NULL DEFAULT 0,"
		"fragmented TINYINT NOT NULL DEFAULT 0,"
		"assembled TINYINT NOT NULL DEFAULT 0,"
		"filter_index INT NOT NULL DEFAULT -1,"
		"group_bit INT NOT NULL DEFAULT -1,"
		"cycle INT NOT NULL DEFAULT -1,"
		"frame INT NOT NULL DEFAULT -1,"
		"INDEX idx_received_utc (received_utc), INDEX idx_address (address), INDEX idx_filtered (filtered)"
		") CHARACTER SET utf8mb4";
	if (!ExecuteDirect(connection, schema, error))
	{
		Close();
		return false;
	}
	dsn_ = dsn;
	username_ = username;
	table_ = table;
	return true;
}

bool MysqlOdbcOutput::Write(const pdw::publishing::PublishEvent& event, std::string& error)
{
	error.clear();
	SQLHDBC connection = static_cast<SQLHDBC>(connection_);
	if (connection == SQL_NULL_HDBC)
	{
		error = "MySQL ODBC output is not connected.";
		return false;
	}
	SQLHSTMT statement = SQL_NULL_HSTMT;
	if (!Succeeded(SQLAllocHandle(SQL_HANDLE_STMT, connection, &statement)))
	{
		error = "MySQL ODBC could not allocate an insert statement.";
		return false;
	}
	SQLSetStmtAttr(statement, SQL_ATTR_QUERY_TIMEOUT,
		reinterpret_cast<SQLPOINTER>(static_cast<ULONG_PTR>(10)), 0);
	const std::string sql =
		"INSERT INTO `" + table_ + "` ("
		"id,received_utc,source,address,local_time,local_date,mode,message_type,bitrate,message,filter_label,"
		"filter_matched,monitor_only,filtered,rejected,blocked_duplicate,group_call,group_final,fragmented,assembled,"
		"filter_index,group_bit,cycle,frame) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
		"ON DUPLICATE KEY UPDATE id=id";
	SQLRETURN result = SQLPrepareA(statement,
		reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())), SQL_NTS);
	if (!Succeeded(result))
	{
		error = Diagnostics(SQL_HANDLE_STMT, statement, "MySQL ODBC could not prepare its insert.");
		SQLFreeHandle(SQL_HANDLE_STMT, statement);
		return false;
	}
	std::vector<std::string> text;
	text.push_back(event.id); text.push_back(event.timestamp); text.push_back(event.source);
	text.push_back(event.address); text.push_back(event.time); text.push_back(event.date);
	text.push_back(event.mode); text.push_back(event.messageType); text.push_back(event.bitrate);
	text.push_back(event.message); text.push_back(event.filterLabel);
	SQLLEN textLengths[11] = {};
	bool bound = true;
	for (SQLUSMALLINT index = 0; index < 11; ++index)
	{
		const SQLSMALLINT sqlType = index == 9 ? SQL_LONGVARCHAR : SQL_VARCHAR;
		const SQLULEN columnSize = index == 9 ? 16777215 : (index == 10 ? 65535 : 512);
		bound = bound && BindText(statement, index + 1, text[index], textLengths[index], columnSize, sqlType);
	}
	SQLINTEGER integers[13] = {
		event.filterMatched ? 1 : 0, event.monitorOnly ? 1 : 0, event.filtered ? 1 : 0,
		event.rejected ? 1 : 0, event.blockedDuplicate ? 1 : 0, event.groupCall ? 1 : 0,
		event.groupFinal ? 1 : 0, event.fragmented ? 1 : 0, event.assembled ? 1 : 0,
		event.filterIndex, event.groupBit, event.cycle, event.frame
	};
	SQLLEN integerLengths[13] = {};
	for (SQLUSMALLINT index = 0; index < 13; ++index)
		bound = bound && BindInt(statement, index + 12, integers[index], integerLengths[index]);
	if (!bound)
	{
		error = Diagnostics(SQL_HANDLE_STMT, statement, "MySQL ODBC could not bind an insert value.");
		SQLFreeHandle(SQL_HANDLE_STMT, statement);
		return false;
	}
	result = SQLExecute(statement);
	if (!Succeeded(result)) error = Diagnostics(SQL_HANDLE_STMT, statement, "MySQL ODBC insert failed.");
	SQLFreeHandle(SQL_HANDLE_STMT, statement);
	return Succeeded(result);
}

void MysqlOdbcOutput::Close()
{
	SQLHDBC connection = static_cast<SQLHDBC>(connection_);
	if (connection != SQL_NULL_HDBC)
	{
		SQLDisconnect(connection);
		SQLFreeHandle(SQL_HANDLE_DBC, connection);
	}
	connection_ = SQL_NULL_HDBC;
	SQLHENV environment = static_cast<SQLHENV>(environment_);
	if (environment != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, environment);
	environment_ = SQL_NULL_HENV;
	dsn_.clear();
	username_.clear();
	table_.clear();
}

bool MysqlOdbcOutput::IsOpen() const
{
	return connection_ != SQL_NULL_HDBC;
}

} // namespace outputs
} // namespace pdw
