#ifndef ORA_TYPES_H
#define ORA_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#include <oci.h>

struct ora_conn_ctx;

struct ora_bind_return {
	union {
		uint64_t uint64;
		char *string;
		OCINumber number;
	};
	ub4 rlen;
	ub2 rcode;
	ub2 ind;
};

struct ora_bind {
	OCIBind *bindhp;
	const char *bind_name;
	size_t bind_name_len;
	ub2 type;
	union {
		uint64_t uint64;
		struct {
			char *value;
			size_t len;
		} string;
		OCINumber number;
	};
	sb2 ind;
	ub4 alen;

	struct ora_bind_return *returns;
	ub2 rowsret;
	struct ora_conn_ctx *conn;
};

struct ora_define {
	OCIDefine *defhp;
	char *col_name;
	ub4 col_name_len;
	ub2 col_width;
	ub2 type;
	ub4 char_semantics;
	union {
		double double64;
		int64_t int64;
		uint64_t uint64;
		struct {
			char *value;
			size_t len;
		} string;
		OCINumber number;
		OCIClobLocator *clob;
		OCIBlobLocator *blob;
	};
	sb2 ind;
};

/**
 * Oracle connection context
 */
struct ora_conn_ctx {
	OCIEnv *envhp;
	OCISession *authp;
	OCIServer *srvhp;
	OCISvcCtx *svchp;
	OCIError *errhp;
	OCIStmt *stmthp;
	uint32_t bind_count;
	struct ora_bind *binds;
	uint32_t define_count;
	struct ora_define *defines;
	bool info;
	char message[512];
};


#endif
