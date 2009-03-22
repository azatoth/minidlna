/*  MiniDLNA media server
 *  Copyright (C) 2008-2009  Justin Maggard
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
#include "log.h"

int
sql_exec(sqlite3 * db, const char * sql)
{
	int ret;
	char *errMsg = NULL;
	//DPRINTF(E_DEBUG, L_DB_SQL, "SQL: %s\n", sql);

	ret = sqlite3_exec(db, sql, 0, 0, &errMsg);
	if( ret != SQLITE_OK )
	{
		DPRINTF(E_ERROR, L_DB_SQL, "SQL ERROR %d [%s]\n%s\n", ret, errMsg, sql);
		if (errMsg)
			sqlite3_free(errMsg);
	}
	return ret;
}

int
sql_get_table(sqlite3 *db, const char *sql, char ***pazResult, int *pnRow, int *pnColumn)
{
	int ret;
	char *errMsg = NULL;
	//DPRINTF(E_DEBUG, L_DB_SQL, "SQL: %s\n", sql);
	
	ret = sqlite3_get_table(db, sql, pazResult, pnRow, pnColumn, &errMsg);
	if( ret != SQLITE_OK )
	{
		DPRINTF(E_ERROR, L_DB_SQL, "SQL ERROR %d [%s]\n%s\n", ret, errMsg, sql);
		if (errMsg)
			sqlite3_free(errMsg);
	}
	return ret;
}

