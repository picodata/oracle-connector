#ifndef ORA_ASYNC_H
#define ORA_ASYNC_H

#undef PACKAGE_VERSION
#include <module.h>

#include <oci.h>

static inline ssize_t
oci_stmt_execute_cb(va_list ap)
{
	sword *res = va_arg(ap, sword *);
	OCISvcCtx *svchp = va_arg(ap, OCISvcCtx *);
	OCIStmt *stmthp = va_arg(ap, OCIStmt *);
	OCIError *errhp = va_arg(ap, OCIError *);
	ub4 exec_count = va_arg(ap, ub4);
	*res = OCIStmtExecute(svchp, stmthp, errhp, exec_count, 0, NULL, NULL,
			      OCI_DEFAULT);
	return 0;
}

static inline sword
oci_stmt_execute_coio(OCISvcCtx *svchp, OCIStmt *stmthp, OCIError *errhp,
		      ub4 exec_count)
{
	sword res;
	coio_call(oci_stmt_execute_cb, &res, svchp, stmthp, errhp, exec_count);
	return res;
}

static inline ssize_t
oci_stmt_fetch_cb(va_list ap)
{
	sword *res = va_arg(ap, sword *);
	OCIStmt *stmthp = va_arg(ap, OCIStmt *);
	OCIError *errhp = va_arg(ap, OCIError *);
	ub4 fetch_count = va_arg(ap, ub4);
	*res = OCIStmtFetch(stmthp, errhp, fetch_count, OCI_FETCH_NEXT,
			    OCI_DEFAULT);
	return 0;
}

static inline sword
oci_stmt_fetch_coio(OCIStmt *stmthp, OCIError *errhp, ub4 fetch_count)
{
	sword res;
	coio_call(oci_stmt_fetch_cb, &res, stmthp, errhp, fetch_count);
	return res;
}

static inline ssize_t
oci_blob_read_cb(va_list ap)
{
	sword *res = va_arg(ap, sword *);
	OCISvcCtx *svchp = va_arg(ap, OCISvcCtx *);
	OCIError *errhp = va_arg(ap, OCIError *);
	OCIBlobLocator *blob = va_arg(ap, OCIBlobLocator *);
	void *buffer = va_arg(ap, void *);
	ub4 *data_read = va_arg(ap, ub4 *);
	ub4 length = va_arg(ap, ub4);
	*res = OCILobRead(svchp, errhp, blob, data_read, (ub4)1, buffer,
			  length, (void *)NULL, (OCICallbackLobRead)NULL,
			  (ub2)0, (ub1)0);
	return 0;
}

static inline sword
oci_blob_read_coio(OCISvcCtx *svchp, OCIError *errhp, OCIClobLocator *blob,
		   void *buffer, ub4 *data_read, ub4 length)
{
	sword res;
	coio_call(oci_blob_read_cb, &res, svchp, errhp, blob, buffer,
		  data_read, length);
	return res;
}

static inline ssize_t
oci_clob_read_cb(va_list ap)
{
	sword *res = va_arg(ap, sword *);
	OCISvcCtx *svchp = va_arg(ap, OCISvcCtx *);
	OCIError *errhp = va_arg(ap, OCIError *);
	OCIClobLocator *clob = va_arg(ap, OCIClobLocator *);
	void *buffer = va_arg(ap, void *);
	ub4 *data_read = va_arg(ap, ub4 *);
	ub4 length = va_arg(ap, ub4);
	ub1 clob_cs = va_arg(ap, unsigned int);
	*res = OCILobRead(svchp, errhp, clob, data_read, (ub4)1, buffer,
			  length, (void *)NULL, (OCICallbackLobRead)NULL,
			  (ub2)0, clob_cs);
	return 0;
}

static inline sword
oci_clob_read_coio(OCISvcCtx *svchp, OCIError *errhp, OCIClobLocator *blob,
		   void *buffer, ub4 *data_read, ub4 length, ub1 clob_cs)
{
	sword res;
	coio_call(oci_clob_read_cb, &res, svchp, errhp, blob, buffer,
		  data_read, length, (unsigned int)clob_cs);
	return res;
}

static inline ssize_t
oci_server_attach_cb(va_list ap)
{
	sword *res = va_arg(ap, sword *);
	OCIServer *srvhp = va_arg(ap, OCIServer *);
	OCIError *errhp = va_arg(ap, OCIError *);
	text *dbname = va_arg(ap, text *);
	sb4 dbname_len = va_arg(ap, sb4);
	*res = OCIServerAttach(srvhp, errhp, dbname, dbname_len, 0);
	return 0;
}

static inline sword
oci_server_attach_coio(OCIServer *srvhp, OCIError *errhp, text *dbname,
		       sb4 dbname_len)
{
	sword res;
	coio_call(oci_server_attach_cb, &res, srvhp, errhp, dbname, dbname_len);
	return res;
}

static inline ssize_t
oci_session_begin_cb(va_list ap)
{
	sword *res = va_arg(ap, sword *);
	OCISvcCtx *svchp = va_arg(ap, OCISvcCtx *);
	OCIError *errhp = va_arg(ap, OCIError *);
	OCISession *authp = va_arg(ap, OCISession *);
	*res = OCISessionBegin(svchp, errhp, authp, OCI_CRED_RDBMS, OCI_DEFAULT);
	return 0;
}

static inline sword
oci_session_begin_coio(OCISvcCtx *svchp, OCIError *errhp, OCISession *authp)
{
	sword res;
	coio_call(oci_session_begin_cb, &res, svchp, errhp, authp);
	return res;
}

#endif
