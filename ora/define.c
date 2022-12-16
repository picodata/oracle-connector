#include "define.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

void
ora_free_defines(struct ora_conn_ctx *conn)
{
	for (ub4 col_index = 1; col_index <= conn->define_count; ++col_index) {
		struct ora_define *define = conn->defines + col_index - 1;
		if (define->defhp)
			(void) OCIHandleFree((dvoid *)define->defhp, (ub4)OCI_HTYPE_DEFINE);

		switch (define->type) {
		case OCI_TYPECODE_VARCHAR:
		case OCI_TYPECODE_VARCHAR2:
			if (define->string.value != NULL)
				free(define->string.value);
			break;

		case OCI_TYPECODE_NUMBER:
			break;

		case OCI_TYPECODE_REAL:
		case OCI_TYPECODE_DOUBLE:
			break;

		case OCI_TYPECODE_OCTET:
		case OCI_TYPECODE_UNSIGNED8:
		case OCI_TYPECODE_UNSIGNED16:
		case OCI_TYPECODE_UNSIGNED32:
			break;

		case OCI_TYPECODE_SIGNED8:
		case OCI_TYPECODE_SIGNED16:
		case OCI_TYPECODE_SIGNED32:
		case OCI_TYPECODE_SMALLINT:
		case OCI_TYPECODE_INTEGER:
			break;

		case OCI_TYPECODE_BLOB:
			OCIDescriptorFree((dvoid *)define->blob, (ub4)OCI_DTYPE_LOB);
			break;

		case OCI_TYPECODE_CLOB:
			OCIDescriptorFree((dvoid *)define->blob, (ub4)OCI_DTYPE_LOB);
			break;

		default:
			if (define->string.value)
				free(define->string.value);
			break;
		}
	}
}

static int
ora_describe(struct ora_conn_ctx *conn)
{
	sword errcode;

	OCIParam *mypard = (OCIParam *)0;
	ub4 col_count = 0;
	errcode =  OCIParamGet((void *)conn->stmthp, OCI_HTYPE_STMT, conn->errhp,
			       (void **)&mypard, (ub4)col_count + 1);
	while (errcode == OCI_SUCCESS) {
		col_count++;
		errcode = OCIParamGet((void *)conn->stmthp, OCI_HTYPE_STMT,
				      conn->errhp, (void **)&mypard,
				      (ub4)col_count + 1);
	}
	if (errcode == OCI_ERROR) {
		sb4 errcode;
		char errmsg[ERRBUF_SIZE];
		(void) OCIErrorGet((dvoid *)conn->errhp, (ub4)1, (text *)NULL,
				   &errcode, (text *)errmsg, (ub4)sizeof(errmsg),
				   OCI_HTYPE_ERROR);
		if (errcode != 24334) {
			checkerror(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info);
			return -1;
		}
	}

	struct ora_define *defines =
		(struct ora_define *)malloc(sizeof(struct ora_define) * col_count);
	if (defines == NULL) {
		snprintf(conn->message, sizeof(conn->message), "%s %lu %s",
			 "could not allocate ",
			 sizeof(struct ora_define) * col_count, "bytes");
		return -1;
	}
	memset(defines, 0, sizeof(struct ora_define) * col_count);

	for (ub4 col_index = 1; col_index <= col_count; ++col_index) {
		errcode =  OCIParamGet((void *)conn->stmthp, OCI_HTYPE_STMT, conn->errhp,
				       (void **)&mypard, (ub4)col_index);
		CHECK_AND_GOTO(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info, fail_describe);

		struct ora_define *define = defines + col_index - 1;

		errcode = OCIAttrGet((void*)mypard, (ub4)OCI_DTYPE_PARAM,
				     (void*)&define->type, (ub4 *)0,
				     (ub4)OCI_ATTR_DATA_TYPE,
				     (OCIError *)conn->errhp);
		CHECK_AND_GOTO(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info, fail_describe);

		errcode = OCIAttrGet((void*)mypard, (ub4)OCI_DTYPE_PARAM,
				     (void**)&define->col_name,
				     (ub4 *)&define->col_name_len,
				     (ub4)OCI_ATTR_NAME,
				     (OCIError *)conn->errhp);
		CHECK_AND_GOTO(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info, fail_describe);

		/* Retrieve the length semantics for the column */
		errcode = OCIAttrGet((void*)mypard, (ub4)OCI_DTYPE_PARAM,
				     (void*)&define->char_semantics, (ub4 *)0,
				     (ub4)OCI_ATTR_CHAR_USED,
				     (OCIError *)conn->errhp);
		CHECK_AND_GOTO(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info, fail_describe);

		if (define->char_semantics) {
			/* Retrieve the column width in characters */
			errcode =  OCIAttrGet((void*)mypard, (ub4)OCI_DTYPE_PARAM,
					      (void*)&define->col_width, (ub4 *)0,
					      (ub4)OCI_ATTR_CHAR_SIZE,
					      (OCIError *)conn->errhp);
			CHECK_AND_GOTO(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info, fail_describe);
		} else {
			/* Retrieve the column width in bytes */
			errcode = OCIAttrGet((void*)mypard, (ub4) OCI_DTYPE_PARAM,
					     (void*)&define->col_width, (ub4 *)0,
					     (ub4)OCI_ATTR_DATA_SIZE,
					     (OCIError *)conn->errhp);
			CHECK_AND_GOTO(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info, fail_describe);
		}
	}
	conn->defines = defines;
	conn->define_count = col_count;

	return 0;

fail_describe:
	free(defines);
	return -1;
}

int
ora_make_defines(struct ora_conn_ctx *conn)
{
	sword errcode;

	if (ora_describe(conn))
		return -1;

	for (ub4 col_index = 1; col_index <= conn->define_count; ++col_index) {
		struct ora_define *define = conn->defines + col_index - 1;

		switch (define->type) {
		case OCI_TYPECODE_VARCHAR:
		case OCI_TYPECODE_VARCHAR2:
			define->string.value = (char*)malloc(define->col_width * 4);
			if (define->string.value == NULL) {
				snprintf(conn->message, sizeof(conn->message),
					 "%s %u %s", "could not allocate ",
					 define->col_width * 4, "bytes");
				goto fail_defines;
			}
			errcode = OCIDefineByPos(conn->stmthp, &define->defhp,
						 conn->errhp, col_index,
						 (dvoid *)define->string.value,
						 (sb4)define->col_width,
						 SQLT_AFC,
						 (dvoid *)&define->ind, (ub2 *)&define->string.len,
						 (ub2 *)0, OCI_DEFAULT);
			break;

		case OCI_TYPECODE_NUMBER:
			errcode = OCIDefineByPos(conn->stmthp, &define->defhp,
						 conn->errhp, col_index,
						 (dvoid *)&define->number,
						 (sb4)sizeof(define->number),
						 SQLT_VNU,
						 (dvoid *)&define->ind, (ub2 *)0,
						 (ub2 *)0, OCI_DEFAULT);
			break;

		case OCI_TYPECODE_REAL:
		case OCI_TYPECODE_DOUBLE:
			errcode = OCIDefineByPos(conn->stmthp, &define->defhp,
						 conn->errhp, col_index,
						 (dvoid *)&define->int64,
						 (sb4)sizeof(define->double64),
						 SQLT_INT,
						 (dvoid *)&define->ind, (ub2 *)0,
						 (ub2 *)0, OCI_DEFAULT);
			break;

		case OCI_TYPECODE_OCTET:
		case OCI_TYPECODE_UNSIGNED8:
		case OCI_TYPECODE_UNSIGNED16:
		case OCI_TYPECODE_UNSIGNED32:
			errcode = OCIDefineByPos(conn->stmthp, &define->defhp,
						 conn->errhp, col_index,
						 (dvoid *)&define->int64,
						 (sb4)sizeof(define->uint64),
						 SQLT_UIN,
						 (dvoid *)&define->ind, (ub2 *)0,
						 (ub2 *)0, OCI_DEFAULT);
			break;

		case OCI_TYPECODE_SIGNED8:
		case OCI_TYPECODE_SIGNED16:
		case OCI_TYPECODE_SIGNED32:
		case OCI_TYPECODE_SMALLINT:
		case OCI_TYPECODE_INTEGER:
			errcode = OCIDefineByPos(conn->stmthp, &define->defhp,
						 conn->errhp, col_index,
						 (dvoid *)&define->int64,
						 (sb4)sizeof(define->int64),
						 SQLT_INT,
						 (dvoid *)&define->ind, (ub2 *)0,
						 (ub2 *)0, OCI_DEFAULT);
			break;

		case OCI_TYPECODE_BLOB:
			errcode = OCIDescriptorAlloc(conn->envhp, (dvoid **)&define->blob,
						     (ub4)OCI_DTYPE_LOB, (size_t)0,
						     (dvoid **)0);

			CHECK_AND_GOTO(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info, fail_defines);

			errcode = OCIDefineByPos(conn->stmthp, &define->defhp,
						 conn->errhp, col_index,
						 (dvoid *)&define->blob,
						 (sb4)0,
						 SQLT_BLOB,
						 (dvoid *)&define->ind, (ub2 *)0,
						 (ub2 *)0, OCI_DEFAULT);
			break;

		case OCI_TYPECODE_CLOB:
			errcode = OCIDescriptorAlloc(conn->envhp, (dvoid **)&define->clob,
						     (ub4)OCI_DTYPE_LOB, (size_t)0,
						     (dvoid **)0);

			CHECK_AND_GOTO(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info, fail_defines);

			errcode = OCIDefineByPos(conn->stmthp, &define->defhp,
						 conn->errhp, col_index,
						 (dvoid *)&define->clob,
						 (sb4)0,
						 SQLT_CLOB,
						 (dvoid *)&define->ind, (ub2 *)0,
						 (ub2 *)0, OCI_DEFAULT);
			break;

		default:
			define->string.value = (char*)malloc(define->col_width * 4);
			if (define->string.value == NULL) {
				snprintf(conn->message, sizeof(conn->message),
					 "%s %u %s", "could not allocate ",
					 define->col_width * 4, "bytes");
				goto fail_defines;
			}
			errcode = OCIDefineByPos(conn->stmthp, &define->defhp,
						 conn->errhp, col_index,
						 (dvoid *)define->string.value,
						 (sb4)define->col_width,
						 SQLT_AFC,
						 (dvoid *)&define->ind, (ub2 *)&define->string.len,
						 (ub2 *)0, OCI_DEFAULT);
			break;
		}
		CHECK_AND_GOTO(errcode, conn->errhp, conn->message, sizeof(conn->message), &conn->info, fail_defines);

	}

	return 0;

fail_defines:
	ora_free_defines(conn);

	return -1;
}
