/*  MiniDLNA media server
 *  Copyright (C) 2008  Justin Maggard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>
#include "sql.h"

int
sql_exec(sqlite3 * db, const char * sql)
{
	int ret;
	char *errMsg = NULL;
	//DEBUG printf("SQL: %s\n", sql);

	//ret = sqlite3_exec(db, sql, 0, 0, &errMsg);
	ret = sqlite3_exec(db, sql, 0, 0, &errMsg);
	if( ret != SQLITE_OK )
	{
		fprintf(stderr, "SQL ERROR %d [%s]\n%s\n", ret, errMsg, sql);
		if (errMsg)
			sqlite3_free(errMsg);
	}
	return ret;
}

int
sql_get_table(sqlite3 *db, const char *zSql, char ***pazResult, int *pnRow, int *pnColumn, char **pzErrmsg)
{
	//DEBUG printf("SQL: %s\n", zSql);
	int ret;
	ret = sqlite3_get_table(db, zSql, pazResult, pnRow, pnColumn, pzErrmsg);
	if( ret != SQLITE_OK )
	{
		fprintf(stderr, "SQL ERROR [%s]\n%s\n", *pzErrmsg, zSql);
		if (*pzErrmsg)
			sqlite3_free(*pzErrmsg);
	}
	return ret;
}

