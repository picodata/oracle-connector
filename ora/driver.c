/*
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include <oci.h>

#undef PACKAGE_VERSION
#include <module.h>

#include "types.h"
#include "async.h"
#include "bind.h"
#include "util.h"
#include "define.h"
#include "fetch.h"

static const char ora_driver_label[] = "__tnt_ora_driver";

static inline struct ora_conn_ctx *
lua_check_oraconn(struct lua_State *L, int index)
{
	struct ora_conn_ctx *conn_p = (struct ora_conn_ctx *)luaL_checkudata(L, index, ora_driver_label);
	if (conn_p == NULL || conn_p->svchp == NULL)
		luaL_error(L, "Driver fatal error (closed connection "
			   "or not a connection)");
	return conn_p;
}

/**
 * Push native lua error with code -3
 */
static int
lua_push_error(struct lua_State *L)
{
	lua_pushnumber(L, -3);
	lua_insert(L, -2);
	return 2;
}

/**
 * Start query execution
 */
static int
lua_ora_execute(struct lua_State *L)
{
	struct ora_conn_ctx *conn = lua_check_oraconn(L, 1);

	if (conn->stmthp != NULL) {
		snprintf(conn->message, sizeof(conn->message), "%s",
			 "there is a cursor opened");
		goto fail_stmt;
	}

	conn->info = false;

	if (!lua_isstring(L, 2)) {
		safe_pushstring(L, "Second param should be a sql command");
		return lua_push_error(L);
	}
	const char *sql = lua_tostring(L, 2);

	sword errcode;
	errcode = OCIHandleAlloc((dvoid *)conn->envhp, (dvoid **)&conn->stmthp,
				 OCI_HTYPE_STMT, (size_t)0, (dvoid **)0);
	if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info))
		goto fail_stmt;

	errcode = OCIStmtPrepare(conn->stmthp, conn->errhp, (text *)sql,
				(ub4)strlen(sql),
				(ub4)OCI_NTV_SYNTAX, (ub4)OCI_DEFAULT);
	if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info))
		goto fail_prepare;

	if (ora_make_binds(L, 3, conn))
		goto fail_make_binds;

	if (ora_do_binds(conn))
		goto fail_bind;

	ub2 stmt_type;
	errcode = OCIAttrGet(conn->stmthp, OCI_HTYPE_STMT, (void *)&stmt_type,
			     (ub4 *)0, (ub4)OCI_ATTR_STMT_TYPE,
			     (OCIError *)conn->errhp);
	if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
		goto fail_execute;
	}

	int result;
	ub4 exec_count;
	switch (stmt_type) {
	case OCI_STMT_SELECT:
		exec_count = 0;
		break;
	default:
		exec_count = 1;
		break;
	}

	errcode = oci_stmt_execute_coio(conn->svchp, conn->stmthp, conn->errhp,
					exec_count);

	if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
		goto fail_execute;
	}

	result = 1;
	lua_pushnumber(L, 0);

	if (conn->info)	{
		lua_pushstring(L, conn->message);
		++result;
	} else {
		lua_pushnil(L);
		++result;
	}

	if (exec_count == 0) {
		if (ora_make_defines(conn))
			goto fail_defines;

		if (ora_fetch_and_push_all(L, conn)) {
			ora_free_defines(conn);
			goto fail_fetch;
		}

		++result;
	} else {
		lua_pushnil(L);

		++result;
	}

	if (ora_push_binds(L, conn) > 0)
		++result;

	ora_free_binds(conn);

	(void) OCIHandleFree((dvoid *)conn->stmthp, (ub4)OCI_HTYPE_STMT);
	conn->stmthp = NULL;

	return result;

fail_fetch:

fail_defines:

fail_execute:

fail_make_binds:

fail_bind:
	ora_free_binds(conn);

fail_prepare:
	(void) OCIHandleFree((dvoid *)conn->stmthp, (ub4)OCI_HTYPE_STMT);
	conn->stmthp = NULL;

fail_stmt:
	lua_pushinteger(L, 1);
	int fail = safe_pushstring(L, conn->message);
	return fail ? lua_push_error(L): 2;
}

/**
 * Open cursor
 */
static int
lua_ora_cursor_open(struct lua_State *L)
{
	struct ora_conn_ctx *conn = lua_check_oraconn(L, 1);
	if (conn->stmthp != NULL) {
		snprintf(conn->message, sizeof(conn->message), "%s",
			 "there is a cursor opened");
		goto fail_stmt;
	}

	if (!lua_isstring(L, 2)) {
		safe_pushstring(L, "Second param should be a sql command");
		return lua_push_error(L);
	}

	conn->info = false;

	const char *sql = lua_tostring(L, 2);

	sword errcode;
	errcode = OCIHandleAlloc((dvoid *)conn->envhp, (dvoid **)&conn->stmthp,
				 OCI_HTYPE_STMT, (size_t)0, (dvoid **)0);
	if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info))
		goto fail_stmt;

	errcode = OCIStmtPrepare(conn->stmthp, conn->errhp, (text *)sql,
				(ub4)strlen(sql),
				(ub4)OCI_NTV_SYNTAX, (ub4)OCI_DEFAULT);
	if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info))
		goto fail_prepare;

	if (ora_make_binds(L, 3, conn))
		goto fail_make_binds;

	if (ora_do_binds(conn))
		goto fail_bind;

	ub2 stmt_type;
	errcode = OCIAttrGet(conn->stmthp, OCI_HTYPE_STMT, (void *)&stmt_type,
			     (ub4 *)0, (ub4)OCI_ATTR_STMT_TYPE,
			     (OCIError *)conn->errhp);
	if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
		goto fail_execute;
	}

	if (stmt_type != OCI_STMT_SELECT) {
		snprintf(conn->message, sizeof(conn->message), "%s",
			 "invalid statement type");
		goto fail_execute;
	}

	errcode = OCIStmtExecute(conn->svchp, conn->stmthp, conn->errhp,
				 (ub4)0,
				 (ub4)0, (CONST OCISnapshot *) NULL,
				 (OCISnapshot *) NULL, OCI_DEFAULT);
	if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
		goto fail_execute;
	}

	int result = 1;
	lua_pushnumber(L, 0);

	if (ora_make_defines(conn))
		goto fail_defines;

	ora_free_binds(conn);

	if (conn->info)	{
		lua_pushstring(L, conn->message);
		++result;
	} else {
		lua_pushnil(L);
		++result;
	}

	return result;

fail_defines:
	ora_free_defines(conn);

fail_execute:

fail_make_binds:

fail_bind:
	ora_free_binds(conn);

fail_prepare:
	(void) OCIHandleFree((dvoid *)conn->stmthp, (ub4)OCI_HTYPE_STMT);
	conn->stmthp = NULL;

fail_stmt:
	lua_pushinteger(L, 1);
	int fail = safe_pushstring(L, conn->message);
	return fail ? lua_push_error(L): 2;
}

/**
 * Fetch from cursor
 */
static int
lua_ora_cursor_fetch(struct lua_State *L)
{
	struct ora_conn_ctx *conn = lua_check_oraconn(L, 1);

	if (conn->stmthp == NULL) {
		snprintf(conn->message, sizeof(conn->message), "%s", "there is no open cursor");
		goto error;
	}

	conn->info = NULL;

	lua_pushnumber(L, 0);

	int row_cnt = ora_fetch_row(conn);
	if (row_cnt == 0) {
		ora_free_defines(conn);
		(void) OCIHandleFree((dvoid *)conn->stmthp, (ub4)OCI_HTYPE_STMT);
		conn->stmthp = NULL;
		return 1;
	}

	if (row_cnt < 0)
		goto fail_fetch;

	if (conn->info)
		lua_pushstring(L, conn->message);
	else
		lua_pushnil(L);

	if (ora_push_row(L, conn) < 0)
		goto fail_fetch;

	return 3;

fail_fetch:
	ora_free_defines(conn);
	(void) OCIHandleFree((dvoid *)conn->stmthp, (ub4)OCI_HTYPE_STMT);
	conn->stmthp = NULL;

error:
	lua_pushinteger(L, 1);
	int fail = safe_pushstring(L, conn->message);
	return fail ? lua_push_error(L): 2;
}

/**
 * Close cursor
 */
static int
lua_ora_cursor_close(struct lua_State *L)
{
	struct ora_conn_ctx *conn = lua_check_oraconn(L, 1);

	if (conn->stmthp == NULL)
		return 0;

	ora_free_defines(conn);
	(void) OCIHandleFree((dvoid *)conn->stmthp, (ub4)OCI_HTYPE_STMT);
	conn->stmthp = NULL;
	return 0;
}

/**
 * Close connection
 */
static int
lua_ora_close(struct lua_State *L)
{
	struct ora_conn_ctx *conn = (struct ora_conn_ctx *)luaL_checkudata(L, 1, ora_driver_label);
	if (conn == NULL || conn->svchp == NULL) {
		lua_pushboolean(L, 0);
		return 1;
	}

	if (conn->stmthp != NULL) {
		if (conn->defines != NULL)
			ora_free_defines(conn);
		if (conn->binds != NULL)
			ora_free_binds(0);
		(void) OCIHandleFree((dvoid *)conn->stmthp, (ub4)OCI_HTYPE_STMT);
		conn->stmthp = NULL;
	}

	(void) OCISessionEnd(conn->svchp, conn->errhp, conn->authp, (ub4)0);
	if (conn->srvhp)
		(void) OCIServerDetach(conn->srvhp, conn->errhp, (ub4)OCI_DEFAULT);
	if (conn->srvhp)
		(void) OCIHandleFree((dvoid *)conn->srvhp, (ub4)OCI_HTYPE_SERVER);
	if (conn->svchp)
		(void) OCIHandleFree((dvoid *)conn->svchp, (ub4)OCI_HTYPE_SVCCTX);
	if (conn->errhp)
		(void) OCIHandleFree((dvoid *)conn->errhp, (ub4)OCI_HTYPE_ERROR);
	if (conn->authp)
		(void) OCIHandleFree((dvoid *)conn->authp, (ub4)OCI_HTYPE_SESSION);
	if (conn->envhp)
		(void) OCIHandleFree((dvoid *)conn->envhp, (ub4)OCI_HTYPE_ENV);

	conn->svchp = NULL;
	lua_pushboolean(L, 1);
	return 1;
}

/**
 * Collect connection
 */
static int
lua_ora_gc(struct lua_State *L)
{
	struct ora_conn_ctx *conn = (struct ora_conn_ctx *)luaL_checkudata(L, 1, ora_driver_label);
	if (conn == NULL || conn->svchp == NULL)
		return 0;

	if (conn->stmthp != NULL) {
		if (conn->defines != NULL)
			ora_free_defines(conn);
		if (conn->binds != NULL)
			ora_free_binds(0);
		(void) OCIHandleFree((dvoid *)conn->stmthp, (ub4)OCI_HTYPE_STMT);
		conn->stmthp = NULL;
	}

	(void) OCISessionEnd(conn->svchp, conn->errhp, conn->authp, (ub4)0);
	if (conn->srvhp)
		(void) OCIServerDetach(conn->srvhp, conn->errhp, (ub4)OCI_DEFAULT);
	if (conn->srvhp)
		(void) OCIHandleFree((dvoid *) conn->srvhp, (ub4) OCI_HTYPE_SERVER);
	if (conn->svchp)
		(void) OCIHandleFree((dvoid *) conn->svchp, (ub4) OCI_HTYPE_SVCCTX);
	if (conn->errhp)
		(void) OCIHandleFree((dvoid *) conn->errhp, (ub4) OCI_HTYPE_ERROR);
	if (conn->authp)
		(void) OCIHandleFree((dvoid *) conn->authp, (ub4) OCI_HTYPE_SESSION);
	if (conn->envhp)
		(void) OCIHandleFree((dvoid *) conn->envhp, (ub4) OCI_HTYPE_ENV);

	conn->svchp = NULL;
	return 0;
}

static int
lua_ora_tostring(struct lua_State *L)
{
	struct ora_conn_ctx *conn = lua_check_oraconn(L, 1);
	lua_pushfstring(L, "Oracle connection: %p", conn);
	return 1;
}

/**
 * Start connection to oracle
 */
static int
lua_ora_connect(struct lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isstring(L, 1) || !lua_isstring(L, 2) ||
	    !lua_isstring(L, 3))
		luaL_error(L, "Usage: ora.connect(connstring, username, passwd)");


	const char *dbname = lua_tostring(L, 1);
	const char *username = lua_tostring(L, 2);
	const char *password = lua_tostring(L, 3);

	OCIEnv *envhp = NULL;
	OCIError *errhp = NULL;

	sword errcode;
	char message[ERRBUF_SIZE];
	errcode = OCIEnvNlsCreate((OCIEnv **)&envhp, (ub4)OCI_DEFAULT,
		  (dvoid *)0, (dvoid * (*)(dvoid *,size_t))0,
		  (dvoid * (*)(dvoid *, dvoid *, size_t))0,
		  (void (*)(dvoid *, dvoid *))0, (size_t)0, (dvoid **)0, 873, 873);

	if (errcode != 0) {
		lua_pushinteger(L, -1);
		(void) snprintf(message, strlen(message),
				"could not create environmet, errcode %i",
				errcode);
		goto fail;
	}

	errcode = OCIHandleAlloc((dvoid *)envhp, (dvoid **)&errhp, OCI_HTYPE_ERROR,
				 (size_t)0, (dvoid **)0);
	if (errcode != 0) {

		lua_pushinteger(L, -1);
		snprintf(message, strlen(message),
			 "could not create error handle, errcode %i", errcode);
		goto fail_errhp;
	}

	struct ora_conn_ctx conn_ctx;
	conn_ctx.envhp = envhp;
	conn_ctx.errhp = errhp;
	conn_ctx.stmthp = NULL;
	conn_ctx.bind_count = 0;
	conn_ctx.binds = (struct ora_bind *)NULL;
	conn_ctx.define_count = 0;
	conn_ctx.defines = (struct ora_define *)NULL;
	conn_ctx.info = false;

	/* server contexts */
	errcode = OCIHandleAlloc((dvoid *)envhp, (dvoid **)&conn_ctx.srvhp, OCI_HTYPE_SERVER,
				 (size_t)0, (dvoid **)0);

	if (!checkerror(errcode, conn_ctx.errhp, message, sizeof(message), NULL))
		goto fail_srvhp;

	errcode = OCIHandleAlloc((dvoid *)envhp, (dvoid **)&conn_ctx.svchp, OCI_HTYPE_SVCCTX,
				 (size_t)0, (dvoid **)0);
	if (!checkerror(errcode, conn_ctx.errhp, message, sizeof(message), NULL))
		goto fail_svchp;

	errcode = oci_server_attach_coio(conn_ctx.srvhp, errhp, (text *)dbname,
				  strlen((char *)dbname));
	if (!checkerror(errcode, conn_ctx.errhp, message, sizeof(message), NULL))
		goto fail_attach;

	/* set attribute server context in the service context */
	errcode = OCIAttrSet((dvoid *)conn_ctx.svchp, OCI_HTYPE_SVCCTX, (dvoid *)conn_ctx.srvhp,
			     (ub4)0, OCI_ATTR_SERVER, (OCIError *)errhp);
	if (!checkerror(errcode, conn_ctx.errhp, message, sizeof(message), NULL))
		goto fail_attach;

	errcode = OCIHandleAlloc((dvoid *)conn_ctx.envhp, (dvoid **)&conn_ctx.authp,
				 (ub4)OCI_HTYPE_SESSION, (size_t)0, (dvoid **)0);
	if (!checkerror(errcode, conn_ctx.errhp, message, sizeof(message), NULL))
		goto fail_attach;

	errcode = OCIAttrSet((dvoid *)conn_ctx.authp, (ub4)OCI_HTYPE_SESSION,
			     (dvoid *)username, (ub4)strlen((char *)username),
			     (ub4)OCI_ATTR_USERNAME, errhp);
	if (!checkerror(errcode, conn_ctx.errhp, message, sizeof(message), NULL))
		goto fail_auth;

	errcode = OCIAttrSet((dvoid *)conn_ctx.authp, (ub4)OCI_HTYPE_SESSION,
			     (dvoid *)password, (ub4)strlen((char *)password),
			     (ub4)OCI_ATTR_PASSWORD, errhp);
	if (!checkerror(errcode, conn_ctx.errhp, message, sizeof(message), NULL))
		goto fail_auth;

	errcode = oci_session_begin_coio(conn_ctx.svchp, errhp, conn_ctx.authp);
	if (!checkerror(errcode, conn_ctx.errhp, message, sizeof(message), NULL))
		goto fail_auth;

	errcode = OCIAttrSet((dvoid *)conn_ctx.svchp, (ub4)OCI_HTYPE_SVCCTX,
			     (dvoid *)conn_ctx.authp, (ub4)0,
			     (ub4)OCI_ATTR_SESSION, errhp);
	if (!checkerror(errcode, conn_ctx.errhp, message, sizeof(message), NULL))
		goto fail_auth;

	lua_pushinteger(L, 1);
	struct ora_conn_ctx *conn_p =
		(struct ora_conn_ctx *)lua_newuserdata(L, sizeof(struct ora_conn_ctx));
	*conn_p = conn_ctx;
	luaL_getmetatable(L, ora_driver_label);
	lua_setmetatable(L, -2);
	return 2;


fail_auth:
	OCIHandleFree((dvoid *)conn_ctx.authp, (ub4)OCI_HTYPE_SESSION);

fail_attach:
	OCIHandleFree((dvoid *)conn_ctx.svchp, (ub4)OCI_HTYPE_SVCCTX);

fail_svchp:
	OCIHandleFree((dvoid *)conn_ctx.srvhp, (ub4)OCI_HTYPE_SERVER);

fail_srvhp:
	OCIHandleFree((dvoid *)conn_ctx.errhp, (ub4)OCI_HTYPE_ERROR);

fail_errhp:
	OCIHandleFree((dvoid *)conn_ctx.envhp, (ub4)OCI_HTYPE_ENV);

fail:
	lua_pushinteger(L, -1);
	int fail = safe_pushstring(L, message);
	return fail ? lua_push_error(L): 2;
}

LUA_API int
luaopen_ora_driver(lua_State *L)
{
	static const struct luaL_Reg methods [] = {
		{"execute",	 lua_ora_execute},
		{"cursor_open",	 lua_ora_cursor_open},
		{"cursor_fetch", lua_ora_cursor_fetch},
		{"cursor_close", lua_ora_cursor_close},
		{"close",	 lua_ora_close},
		{"__tostring",	 lua_ora_tostring},
		{"__gc",	 lua_ora_gc},
		{NULL, NULL}
	};

	luaL_newmetatable(L, ora_driver_label);
	lua_pushvalue(L, -1);
	luaL_register(L, NULL, methods);
	lua_setfield(L, -2, "__index");
	lua_pushstring(L, ora_driver_label);
	lua_setfield(L, -2, "__metatable");
	lua_pop(L, 1);

	lua_newtable(L);
	static const struct luaL_Reg meta [] = {
		{"connect", lua_ora_connect},
		{NULL, NULL}
	};
	luaL_register(L, NULL, meta);
	return 1;
}
