#ifndef ORA_UTIL_H
#define ORA_UTIL_H

#include <stdbool.h>

#include <lua.h>
#include <lauxlib.h>

#include <oci.h>

#define ERRBUF_SIZE 512

int
save_pushstring_wrapped(struct lua_State *L);

int
safe_pushstring(struct lua_State *L, char *str);

bool
checkerror(sword status, OCIError *errhp, char *msg, size_t msg_len, bool *info);

#define CHECK_AND_GOTO(STATUS, ERRHP, MSG, MSG_LEN, INFO, LABEL) \
do {if (!checkerror(STATUS, ERRHP, MSG, MSG_LEN, INFO)) goto LABEL;} while (0)

#endif
