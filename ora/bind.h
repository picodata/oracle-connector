#ifndef ORA_BIND_H
#define ORA_BIND_H

#include <lua.h>
#include <lauxlib.h>

#include <oci.h>

#include "types.h"

void
ora_free_binds(struct ora_conn_ctx *conn);

int
ora_make_binds(struct lua_State *L, int params_table, struct ora_conn_ctx *conn);


sb4
ora_bind_input(void *ictxp, OCIBind *bindp, ub4 iter, ub4 index, void **bufpp,
               ub4 *alenp, ub1 *piecep, void **indp);

sb4
ora_bind_output(void *octxp, OCIBind *bindp, ub4 iter, ub4 index, void **bufp,
                ub4 **alenpp, ub1 *piecep, void **indpp, ub2 **rcodepp);

int
ora_do_binds(struct ora_conn_ctx *conn);

int
ora_push_binds(struct lua_State *L, struct ora_conn_ctx *conn);

#endif
