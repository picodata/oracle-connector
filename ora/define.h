#ifndef ORA_DEFINE_H
#define ORA_DEFINE_H

#include "types.h"

void
ora_free_defines(struct ora_conn_ctx *conn);

int
ora_make_defines(struct ora_conn_ctx *conn);

#endif
