/* Reusable SQLite3 wrapper functions
 *
 * Project : minidlna
 * Website : http://sourceforge.net/projects/minidlna/
 * Author  : Justin Maggard
 * Copyright (c) 2008 Justin Maggard
 * This software is subject to the conditions detailed in the
 * LICENCE file provided in this distribution.
 * */
#ifndef __SQL_H__
#define __SQL_H__

#include <sqlite3.h>

int
sql_exec(sqlite3 * db, const char * sql);

int
sql_get_table(sqlite3 *db, const char *zSql, char ***pazResult, int *pnRow, int *pnColumn);

#endif
