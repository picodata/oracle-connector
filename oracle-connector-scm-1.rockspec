package = 'oracle-connector'
version = 'scm-1'

source  = {
    url = "git://github.com/picodata/oracle-connector.git",
    branch = 'main';
}

description = {
    summary  = "Oracle driver for Tarantool";
}

dependencies = {
    'tarantool',
    'lua >= 5.1',
}

build = {
    type = 'cmake',

    variables = {
        TARANTOOL_DIR="$(TARANTOOL_DIR)";
        TARANTOOL_INSTALL_LIBDIR="$(LIBDIR)";
        TARANTOOL_INSTALL_LUADIR="$(LUADIR)";
        PROJECT_NAME = "oracle-connector";
    },
}
