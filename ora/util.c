#include "util.h"

#include <stdio.h>

#include <lua.h>
#include <lauxlib.h>

int
save_pushstring_wrapped(struct lua_State *L)
{
	char *str = (char *)lua_topointer(L, 1);
	lua_pushstring(L, str);
	return 1;
}

int
safe_pushstring(struct lua_State *L, char *str)
{
	lua_pushcfunction(L, save_pushstring_wrapped);
	lua_pushlightuserdata(L, str);
	return lua_pcall(L, 1, 1, 0);
}



bool
checkerror(sword status, OCIError *errhp, char *msg, size_t msg_len, bool *info) {
	if (status == OCI_SUCCESS)
		return true;

	sb4 errcode;
	if (status == OCI_SUCCESS_WITH_INFO) {
		(void) OCIErrorGet((dvoid *)errhp, (ub4)1, (text *)NULL,
				   &errcode, (text *)msg, (ub4)msg_len,
				   OCI_HTYPE_ERROR);
		if (info != NULL)
			*info = true;
		return true;
	}

	char errmsg[ERRBUF_SIZE] = "unknown message";

	switch (status)
	{
	case OCI_NEED_DATA:
		(void) snprintf(msg, msg_len, "Error - OCI_NEED_DATA");
		break;
	case OCI_NO_DATA:
		(void) snprintf(msg, msg_len, "Error - OCI_NODATA");
		break;
	case OCI_ERROR:
		(void) OCIErrorGet((dvoid *)errhp, (ub4)1, (text *)NULL,
				   &errcode, (text *)errmsg, (ub4)sizeof(errmsg),
				   OCI_HTYPE_ERROR);
		(void) snprintf(msg, msg_len, "code %i, message %s", errcode, errmsg);
		break;
	case OCI_INVALID_HANDLE:
		(void) snprintf(msg, msg_len, "Error - OCI_INVALID_HANDLE");
		 break;
	case OCI_STILL_EXECUTING:
		(void) snprintf(msg, msg_len, "Error - OCI_STILL_EXECUTE");
		break;
	case OCI_CONTINUE:
		(void) snprintf(msg, msg_len, "Error - OCI_CONTINUE");
		break;
	default:
		(void) snprintf(msg, msg_len, "Error - %i", status);
		break;
	}
	return false;
}

