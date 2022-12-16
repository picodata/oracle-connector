#include "fetch.h"

#include <stdlib.h>

#include "async.h"
#include "util.h"

int
ora_fetch_row(struct ora_conn_ctx *conn)
{
	sword errcode;

	errcode = oci_stmt_fetch_coio(conn->stmthp, conn->errhp, 1);
	if (errcode == OCI_NO_DATA)
		return 0;

	if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info))
		return -1;
	return 1;
}

int
ora_push_row(struct lua_State *L, struct ora_conn_ctx *conn)
{
	sword errcode;
	boolean is_int;

	lua_newtable(L);

	for (ub4 col_index = 0; col_index < conn->define_count; ++col_index) {
		struct ora_define *define = conn->defines + col_index;
		lua_pushlstring(L, define->col_name, define->col_name_len);
		switch (define->type) {
		case OCI_TYPECODE_VARCHAR:
		case OCI_TYPECODE_VARCHAR2:
			lua_pushlstring(L, define->string.value,
					define->string.len);
			break;

		case OCI_TYPECODE_NUMBER:
			errcode = OCINumberIsInt(conn->errhp, &define->number,
						 &is_int);
			if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
				return -1;
			}

			if (is_int) {
				ub8 inum;
				errcode = OCINumberToInt(conn->errhp,
							 &define->number,
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
							  &define->number,
							  sizeof(dnum),
							  &dnum);
				if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
					return -1;
				}

				lua_pushnumber(L, dnum);
			}
			break;

		case OCI_TYPECODE_REAL:
		case OCI_TYPECODE_DOUBLE:
			lua_pushnumber(L, define->double64);
			break;

		case OCI_TYPECODE_OCTET:
		case OCI_TYPECODE_UNSIGNED8:
		case OCI_TYPECODE_UNSIGNED16:
		case OCI_TYPECODE_UNSIGNED32:
			lua_pushinteger(L, define->uint64);
			break;

		case OCI_TYPECODE_SIGNED8:
		case OCI_TYPECODE_SIGNED16:
		case OCI_TYPECODE_SIGNED32:
		case OCI_TYPECODE_SMALLINT:
		case OCI_TYPECODE_INTEGER:
			lua_pushinteger(L, define->int64);
			break;

		case OCI_TYPECODE_BLOB: {
			ub4 lob_length;
			ub4 data_read;
			errcode = OCILobGetLength(conn->svchp, conn->errhp,
						  define->blob, &lob_length);
			if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info))
				return -1;

			data_read = lob_length;
			void *buffer = malloc(lob_length);
			if (buffer == NULL) {
				snprintf(conn->message, sizeof(conn->message),
					 "%s %u %s", "could not allocate ",
					 lob_length, "bytes");
				return -1;
			}
			errcode = oci_blob_read_coio(conn->svchp, conn->errhp,
						     define->blob, &data_read,
						     buffer, lob_length);
			if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
				free(buffer);
				return -1;
			}

			lua_pushlstring(L, buffer, data_read);
			free(buffer);
			break;
		}

		case OCI_TYPECODE_CLOB: {
			ub1 lob_cs;
			errcode = OCILobCharSetForm(conn->envhp, conn->errhp, define->clob, &lob_cs);
			ub4 lob_length;
			ub4 data_read;
			errcode = OCILobGetLength(conn->svchp, conn->errhp,
						  define->blob, &lob_length);
			if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info))
				return -1;

			data_read = lob_length;
			void *buffer = malloc(lob_length * 4);
			if (buffer == NULL) {
				snprintf(conn->message, sizeof(conn->message),
					 "%s %u %s", "could not allocate ",
					 lob_length, "bytes");
				return -1;
			}
			errcode = oci_clob_read_coio(conn->svchp, conn->errhp,
						     define->clob, &data_read,
						     buffer, lob_length * 4,
						     (ub1)lob_cs);
			if (!checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info)) {
				free(buffer);
				return -1;
			}

			lua_pushlstring(L, buffer, data_read);
			free(buffer);
			break;
		}

		default:
			snprintf(conn->message, sizeof(conn->message),
				 "UNREACHABLE: invalid DEFINE type %d\n", define->type);
			return -1;
		}
		lua_settable(L, -3);
	}

	return 1;
}

int
ora_fetch_and_push_all(struct lua_State *L, struct ora_conn_ctx *conn)
{
	int row = 0;
	lua_newtable(L);

	int fetched = ora_fetch_row(conn);
	while (fetched == 1) {
		lua_pushnumber(L, row + 1);

		if (ora_push_row(L, conn) < 0) {
			lua_pop(L, 1);
			return -1;
		}
		lua_settable(L, -3);

		++row;
		fetched = ora_fetch_row(conn);
	}

	return fetched;
}


