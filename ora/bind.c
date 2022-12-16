#include "bind.h"

#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "util.h"

void
ora_free_binds(struct ora_conn_ctx *conn) {
	for (uint32_t idx = 0; idx < conn->bind_count; ++idx)
	{
		struct ora_bind *bind = conn->binds + idx;
		if (bind->bindhp)
			(void) OCIHandleFree((dvoid *)bind->bindhp, (ub4)OCI_HTYPE_BIND);
		switch (bind->type) {
		case SQLT_AFC:
			free(bind->string.value);
			break;
		}
		for (uint32_t idx = 0; idx < bind->rowsret; ++idx) {
			struct ora_bind_return *ret = bind->returns + idx;
			switch (bind->type) {
			case SQLT_AFC:
				free(ret->string);
				break;
			}
		}
		free(bind->returns);
		bind->rowsret = 0;
	}
	free(conn->binds);
	conn->binds = (struct ora_bind *)NULL;
	conn->bind_count = 0;
}

int
ora_make_binds(struct lua_State *L, int params_table, struct ora_conn_ctx *conn) {
	lua_pushnil(L);  /* first key */
	while (lua_next(L, params_table) != 0) {
		struct ora_bind *binds =
			(struct ora_bind *)realloc(conn->binds,
						   sizeof(struct ora_bind) *
						   (conn->bind_count + 1));
		if (binds == NULL)
			goto fail_bind;

		conn->binds = binds;
		struct ora_bind *bind = conn->binds + conn->bind_count;

		bind->conn = conn;
		bind->type = 0;
		bind->alen = 0;
		bind->ind = -1;
		bind->string.value = NULL;
		bind->string.len = 0;
		bind->bindhp = NULL;
		bind->returns = NULL;
		bind->rowsret = 0;

		bind->bind_name = lua_tolstring(L, -2, &bind->bind_name_len);

		if (lua_istable(L, -1)) {

			lua_pushstring(L, "value");
			lua_gettable(L, -2);
			if (lua_isnil(L, -1)) {
				bind->ind = -1;
			} else {
				bind->ind = 0;
				switch (lua_type(L, -1)) {
				case LUA_TNUMBER:
					bind->type = SQLT_VNU;
					bind->alen = sizeof(bind->number);
					double number = lua_tonumber(L, -1);
					OCINumberFromReal(conn->errhp, &number, sizeof(number), &bind->number);

					break;
				case LUA_TBOOLEAN:
					bind->type = SQLT_UIN;
					bind->alen = sizeof(bind->uint64);

					break;
				case LUA_TSTRING:
				default:
					bind->type = SQLT_AFC;
					size_t len;
					const char *value = lua_tolstring(L, -1, &len);
					bind->alen = len;
					bind->string.value = (char *)malloc(bind->alen);
					if (bind->string.value == NULL) {
						snprintf(conn->message, sizeof(conn->message), "could not allocate %u bytes", bind->alen);
						goto fail_bind;
					}
					bind->string.len = len;
					memcpy(bind->string.value, value, bind->alen);

					break;
				}
			}
			lua_pop(L, 1);

			if (bind->type == 0) {
				lua_pushstring(L, "type");
				lua_gettable(L, -2);
				const char *value = lua_tostring(L, -1);
				if (value == NULL) {
					bind->type = SQLT_AFC;
				} else if (strncmp(value, "string", strlen("string")) == 0) {
					bind->type = SQLT_AFC;
				} else if (strncmp(value, "number", strlen("number")) == 0) {
					bind->type = SQLT_VNU;
					bind->alen = sizeof(bind->number);
					double number = lua_tonumber(L, -1);
					OCINumberFromReal(conn->errhp, &number, sizeof(number), &bind->number);
				} else {
					// Using string implicitly
					bind->type = SQLT_AFC;
				}
				lua_pop(L, 1);
			}

			if (bind->type == SQLT_AFC) {
				lua_pushstring(L, "size");
				lua_gettable(L, -2);
				if (lua_isnumber(L, -1) == 1) {
					bind->alen = lua_tointeger(L, -1);
				}
				lua_pop(L, 1);
			}

		} else if (lua_isboolean(L, -1)) {
			bind->uint64 = lua_toboolean(L, -1) ? 1 : 0;
			bind->type = SQLT_UIN;
			bind->ind = 0;
			bind->alen = sizeof(bind->uint64);

		} else if (lua_type(L, -1) == LUA_TNUMBER) {
			bind->type = SQLT_VNU;
			bind->ind = 0;
			bind->alen = sizeof(bind->number);
			double number = lua_tonumber(L, -1);
			OCINumberFromReal(conn->errhp, &number, sizeof(number), &bind->number);
		} else {
			size_t len;
			const char *value = lua_tolstring(L, -1, &len);
			bind->alen = len;
			bind->string.value = (char *)malloc(bind->alen);
			if (bind->string.value == NULL) {
				snprintf(conn->message, sizeof(conn->message), "could not allocate %u bytes", bind->alen);
				goto fail_bind;
			}
			bind->string.len = len;
			memcpy(bind->string.value, value, bind->alen);

			bind->type = SQLT_AFC;
			bind->ind = 0;
		}
		lua_pop(L, 1);

		++conn->bind_count;
	}

	return 0;

fail_bind:

	return -1;
}

sb4
ora_bind_input(void *ictxp, OCIBind *bindp, ub4 iter, ub4 index, void **bufpp,
               ub4 *alenp, ub1 *piecep, void **indp) {
	(void) bindp;
	struct ora_bind *bind = (struct ora_bind *)ictxp;
	struct ora_conn_ctx *conn = bind->conn;
	(void) iter;
	(void) index;
	switch (bind->type) {
	case SQLT_AFC:
		*bufpp = bind->string.value;
		*alenp = bind->string.len;
		break;
	case SQLT_VNU:
		*bufpp = &bind->number;
		*alenp = sizeof(bind->number);
		break;
	case SQLT_UIN:
		*bufpp = &bind->uint64;
		*alenp = sizeof(bind->uint64);
		break;
	default:
		snprintf(conn->message, sizeof(conn->message),
			 "UNREACHABLE: invalid BIND type %d\n", bind->type);
	}
	*piecep = OCI_ONE_PIECE;
	*indp = &bind->ind;

	return OCI_CONTINUE;
}

sb4
ora_bind_output(void *octxp, OCIBind *bindp, ub4 iter, ub4 index, void **bufp,
                ub4 **alenpp, ub1 *piecep, void **indpp, ub2 **rcodepp) {
	(void) bindp;
	struct ora_bind *bind = (struct ora_bind *)octxp;
	struct ora_conn_ctx *conn = bind->conn;
	(void) iter;

	static ub4 rows = 0;
        if (index == 0) {
		(void) OCIAttrGet(bindp, OCI_HTYPE_BIND, (void *)&rows,
				  (ub4 *)sizeof(ub4), OCI_ATTR_ROWS_RETURNED,
				  bind->conn->errhp);
		if (!rows) {
			// In case of PLSQL assume there is only one returning value
			rows = 1;
		}
		bind->returns =	(struct ora_bind_return *)malloc(sizeof(struct ora_bind_return) * rows);
		if (bind->returns == NULL) {
			snprintf(bind->conn->message, sizeof(bind->conn->message),
				 "could not allocate %u bytes", bind->alen);
			return OCI_ERROR;
		}
		if (!rows) rows = 1;
		memset(bind->returns, 0, sizeof(struct ora_bind_return) * rows);
	        bind->rowsret = (ub2)rows;
	}

	bind->returns[index].rlen = bind->alen;
	switch (bind->type) {
	case SQLT_AFC:
		bind->returns[index].string = (char *)malloc(bind->returns[index].rlen);
		if (bind->returns[index].string == NULL) {
			snprintf(bind->conn->message, sizeof(bind->conn->message),
				 "could not allocate %u bytes", bind->returns[index].rlen);
			return OCI_ERROR;
		}
		*bufp = bind->returns[index].string;
		break;
	case SQLT_VNU:
		*bufp = &bind->returns[index].number;
		break;
	case SQLT_UIN:
		*bufp = &bind->returns[index].uint64;
		break;
	default:
		snprintf(conn->message, sizeof(conn->message),
			 "UNREACHABLE: invalid BIND type %d\n", bind->type);
	}

	*alenpp = &bind->returns[index].rlen;
	*piecep = OCI_ONE_PIECE;
	*indpp = &bind->returns[index].ind;
	*rcodepp = &bind->returns[index].rcode;

	return OCI_CONTINUE;
}

int
ora_do_binds(struct ora_conn_ctx *conn) {
	sb4 errcode;

	for (uint32_t idx = 0; idx < conn->bind_count; ++idx) {
		struct ora_bind *bind = conn->binds + idx;
		struct ora_conn_ctx *conn = bind->conn;
		void *value = NULL;
		switch (bind->type) {
		case SQLT_AFC:
			value = bind->string.value;
			break;
		case SQLT_VNU:
			value = &bind->number;
			break;
		case SQLT_UIN:
			value = &bind->uint64;
			break;
		default:
			snprintf(conn->message, sizeof(conn->message),
				 "UNREACHABLE: invalid BIND type %d\n", bind->type);
		}

		errcode = OCIBindByName(conn->stmthp, &bind->bindhp, conn->errhp,
					(text *)bind->bind_name, bind->bind_name_len,
					(ub1 *)value, bind->alen, bind->type,
					&bind->ind, (ub2 *)0, (ub2 *)0,
					(ub4)0, (ub4 *)0,
					OCI_DATA_AT_EXEC);
		if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
			goto fail_bind;
		}

		errcode = OCIBindDynamic(bind->bindhp, conn->errhp,
					 (void *)bind, ora_bind_input,
					 (void *)bind, ora_bind_output);
		if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
			goto fail_bind;
		}

	}

	return 0;

fail_bind:
	return -1;
}

int
ora_push_binds(struct lua_State *L, struct ora_conn_ctx *conn)
{
	int output = 0;
	sword errcode;
	boolean is_int;

	lua_newtable(L);
	for (uint32_t idx = 0; idx < conn->bind_count; ++idx)
	{
		struct ora_bind *bind = conn->binds + idx;
		if (bind->rowsret == 0)
			continue;
		++output;
		lua_pushlstring(L, bind->bind_name, bind->bind_name_len);
		lua_newtable(L);
		for (ub2 row = 0; row < bind->rowsret; ++row) {
			lua_pushnumber(L, row);
			switch (bind->type) {
			case SQLT_AFC:
				lua_pushlstring(L, bind->returns[row].string,
						bind->returns[row].rlen);
				break;
			case SQLT_VNU:
				errcode = OCINumberIsInt(conn->errhp, &bind->returns[row].number,
							 &is_int);
				if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
					return -1;
				}

				if (is_int) {
					ub8 inum;
					errcode = OCINumberToInt(conn->errhp,
								 &bind->returns[row].number,
								 sizeof(inum),
								 OCI_NUMBER_SIGNED,
								 &inum);
					if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
						return -1;
					}
					lua_pushinteger(L, inum);
				} else {
					double dnum;
					errcode = OCINumberToReal(conn->errhp,
								  &bind->returns[row].number,
								  sizeof(dnum),
								  &dnum);
					if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
						return -1;
					}

					lua_pushnumber(L, dnum);
				}

				break;
			case SQLT_UIN:
				lua_pushinteger(L, bind->returns[row].uint64);
				break;
			}
			lua_settable(L, lua_gettop(L) - 2);
		}
		lua_settable(L, lua_gettop(L) - 2);
	}
	if (output == 0)
		lua_pop(L, 1);
	return output;
}
