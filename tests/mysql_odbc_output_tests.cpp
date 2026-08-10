#include "mysql_odbc_output.h"

#include <iostream>

int main()
{
	// A real MySQL DSN is intentionally not required by automated tests. This
	// verifies the adapter is inert until explicitly configured by the operator.
	pdw::outputs::MysqlOdbcOutput output;
	if (output.IsOpen())
	{
		std::cerr << "MySQL ODBC output connected before configuration.\n";
		return 1;
	}
	std::cout << "MySQL ODBC disabled-state test passed.\n";
	return 0;
}
