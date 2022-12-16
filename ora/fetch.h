#ifndef ORA_FETCH_H
#define ORA_FETCH_H

#include <lua.h>
#include <lauxlib.h>

#include "types.h"


int
ora_fetch_row(struct ora_conn_ctx *conn);

int
ora_push_row(struct lua_State *L, struct ora_conn_ctx *conn);

int
ora_fetch_and_push_all(struct lua_State *L, struct ora_conn_ctx *conn);

#endif
