# ora - Oracle SQL connector for [Tarantool](https://github.com/tarantool/tarantool)

## Getting Started

### Prerequisites

 * Tarantool 2.0.0+ with header files
 * Oracle Client Library with header files

### Installation

Clone repository and then build it using CMake:

``` bash
git clone https://github.com/picodata/oracle-connector.git && cd oracle-connector
mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=RelWithDebugInfo
make
make install
```

## Installation using tarantoolctl
You will need Oracle client libraries installed.

```bash
tarantoolctl rocks --only-server https://download.picodata.io/luarocks/ install oracle-connector <version>
```

### Installation on MacOS

Download the Basic and SDK packages from the [Oracle](https://www.oracle.com/cis/database/technologies/instant-client.html) official site.

Build the connector:

```bash
mkdir build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DORACLE_INCLUDE_DIR=~/Downloads/instantclient_19_8\ 2/sdk/include/ \
  -DORACLE_LIBRARY_DIR=~/Downloads/instantclient_19_8/ \
  -DORACLE_OCI_LIBRARY=~/Downloads/instantclient_19_8/libclntsh.dylib.19.1 \
  -DORACLE_OCCI_LIBRARY=~/Downloads/instantclient_19_8/libocci.dylib.19.1 \
  -DORACLE_NNZ_LIBRARY=~/Downloads/instantclient_19_8/libnnz19.dylib
make install
```

### Oracle database container

1. Download Oracle database 19.3.0 for Linux x86-64 from [Oracle](https://www.oracle.com/database/technologies/oracle-database-software-downloads.html) official site.
1. Clone a repository with Dockerfile and build an image:

   ```bash
   git clone https://github.com/oracle/docker-images.git
   cd docker-images/OracleDatabase/SingleInstance/dockerfiles/19.3.0
   cp ~/Downloads/LINUX.X64_193000_db_home.zip .
   docker build -t oracle/database:19.3.0-se2 --build-arg DB_EDITION=se2 .
   ```

1. Run a container:

   ```bash
   docker run \
    -d --rm --name oracle \
    -p 1521:1521 \
    -p 5500:5500 \
    -p 2484:2484 \
    -e ORACLE_PWD=tntPswd \
    -e ORACLE_SID=tnt \
    -e ORACLE_PDB=SYSTEM \
    oracle/database:19.3.0-se2
   ```


### Usage

``` lua
local ora = require('ora')
local conn = ora.connect({ host = 'localhost', port = '1521', user = 'SYSTEM', pass = 'tntPswd', db = 'tnt', raise = true })
local tuples = conn:execute("SELECT ID, NAME FROM TEST1 WHERE KIND = :KIND", {['KIND'] = 'info'})
conn:close()
```

## API Documentation

### `conn = ora:connect(opts = {})`

Connect to a database.

*Options*:

 - `host` - a hostname to connect
 - `port` - a port number to connect
 - `user` - username
 - `pass` - a password
 - `db` - a database name
 - `raise` - true if an exception should be raised if query execution fails with an error

*Returns*:

 - `connection ~= nil` on success
 - `error(reason)` on error

### `conn:execute(statement, parameters)`

Execute a statement with parameters. Statement could be a normal SQL query string
or PL/SQL anonymous block. Oracle OCI uses ":NAME" as parameter placeholder.
Parameters is a table of input/output parameters with keys matching to all
statement placeholders.
There are two possible formats of a parameter description. The short form consists
only of parameter value whereas long-form is a table describing value, type, and size.

*Returns*:
 - `result set, output variables, true, message` on success
 - `null, null, false, reason` - on error when raise is false
 - `error(reason)` on error when raise is true

A result set has a form of an array of tables like that:
 `{ { column1 = value, column2 = value }, { column1 = value, column2 = value } }, ...`
Output variables are returned in a form of table where each item is an array
of n-th returned value of a variable like that:
 ` { ovar1 = {value, valuse, valuse}, ovar2 = {value, value, value}}`

Result set is returned in case of SELECT statements where output variables are
filled if executing query returning something using variables.

*Examples*:
```
tarantool> conn:execute("CREATE TABLE test1 (ID NUMBER NOT NULL PRIMARY KEY, NAME VARCHAR2(256))")
---
- null
- null
- true
- null

tarantool> conn:execute("INSERT TABLE test1 VALUES (1, 'ONE')")
---
- null
- null
- true
- null

tarantool> conn:execute("INSERT INTO test1 VALUES (:ID, :NAME), {["ID"] = 2, ["NAME"] = {['type'] = "string", ["value"] = "TWO"}})
---
- null
- null
- true
- null

tarantool> conn:execute("UPDATE test1 SET NAME = lower(NAME) RETURNING NAME INTO :NAME", {["NAME"] = {['type'] = "string", ["size"] = 40}})
---
- null
- NAME:
    0: one
    1: two
- true
- null

tarantool> conn:execute("COMMIT")
---
- null
- null
- true
- null

```

#### Type conversion

##### For select operation

The driver does type conversion from Oracle type to lua as described bellow
 * VARCHAR, VARCHAR2 -> lua string
 * Blob -> lua string
 * Clob -> lua string
 * Number -> lua number with detection if then number is integer or fractional
 * Real, Double -> lua number
 * Octet, Unsigned8, Unsigned16, Unsigned32 -> lua number
 * SmallInt, Integer, Signed8, Signed16, Signed32 -> lua number
 * Any other type -> implicit string conversion to lua string

As lua does not have support for builtin types like datas, timestamps then there
is no conversion available expect strings. Please take in mind that such implicit
conversion may depend on database and/or client settings like locale. To make
select result stable it is recommended to use explicit converion within SQL
statement like CAST and CONVERT.
Native driver support for such types is a subject for further development.

##### For paramer binding

Currently the driver supports following type descriptors for parameter binding:
 * string -> the value is got from lua stack as string and binded to a SQL
statement as C NULL-terminated string
 * number -> the value is got from lua stack as lua number ant then binded
as Oracle NUMBER with could be both integer or fractional
 * any other descriptor -> the value is converted to lua string and binded as
C NULL-terminated string
 * if type descriptor is not set or short form for binding values is used then
driver tries to detect following types
 * lua number -> Oracle number
 * lua boolean -> Oracle unsigned
 * lua string - > Oracle C NULL-terminated string
 * any other type is implicitly converted to lua string and then binded as
C NULL-terminated string

### `conn:cursor_open(statement, parameters)`

Execute a select statement but nod fetch data immediately but open a cursor.
For statement and parameters description please refer to execute section.

*Returns*:
 - `true, message` on success
 - `false, reason` on error when raise if false
 - `error(reason)` on error when raise is true

 ### `conn:cursor_fetch()`

Fetches one row from previously opened cursor.
*Returns*:
 - `row, true, message` on success
 - `nill, true, message` on eof
 - `nil, false, reason` on error if raise is false
 - `error(reason)` on error if raise is true

For a row description please refer to execute section

### `conn:cursor_close`
Close previously opened cursor

*Examples*:
```
tarantool> conn:cursor_open("select * from test1")
---
- true
- null

tarantool> conn:cursor_fetch()
---
- NAME: one
  ID: 1
- true
- null

tarantool> conn:cursor_fetch()
---
- NAME: two
  ID: 2
- true
- null

tarantool> conn:cursor_fetch()
---
- null
- true
- null

tarantool> conn:cursor_close()
---
- true

```

### `conn:begin()`

Begin a transaction.

*Returns*: `true`

### `conn:commit()`

Commit current transaction.

*Returns*: `true`

### `conn:rollback()`

Rollback current transaction.

*Returns*: `true`

### `conn:ping()`

Execute a dummy statement to check that connection is alive.

*Returns*:

 - `true` on success
 - `false` on failure

#### `pool = ora.pool_create(opts = {})`

Create a connection pool with count of size established connections.

*Options*:

 - `host` - hostname to connect to
 - `port` - port number to connect to
 - `user` - username
 - `pass` - password
 - `db` - database name
 - `size` - count of connections in pool
 - `raise` - true if an exception should be raised if query execution fails with an error

*Returns*

 - `pool ~=nil` on success
 - `error(reason)` on error

### `conn = pool:get()`

Get a connection from pool. Reset connection before returning it. If connection
is broken then it will be reestablished. If there is no free connections then
calling fiber will sleep until another fiber returns some connection to pool.

*Returns*:

 - `conn ~= nil`

### `pool:put(conn)`

Return a connection to connection pool.

*Options*

 - `conn` - a connection


### How to build a docker container with Oracle database inside

 * Clone the Oracle docker images repository https://github.com/oracle/docker-images and change to it
 * Place linuxx64_12201_database.zip into OracleDatabase/SingleInstance/dockerfiles/12.2.0.1
 * Build image with
   ```./OracleDatabase/SingleInstance/dockerfiles/buildContainerImage.sh -v 12.2.0.1 -s```

Oracle Database is configured on the first run of a contaianer like that:
 ```docker run -p 1521:1521 -e ORACLE_SID=tnt -e ORACLE_PDB=tntPDB -e ORACLE_PWD=tntPswd oracle/database:12.2.0.1-se2```
After the first run container initializes database what could take some time. After
database is configures the container is could be started with simply docker start command the database inside still
needs for some time to finish its startup.

For the full set on available cobfiguration options please reffer to OracleDatabase/SingleInstance/README.md
inside the Oracle docker repository.

### TBD
 * Special handling for binding CLOB and BLOB values. The implementaion requires
for creating BLOB/CLOB handler and provide special read/write callbacks from/to
Lua state machine
 * Special type handling for timestamps, datetimes, tables and may be objects is
a subkect for further discussion and implementation
 * Oracle connection allows to handle multiple oppened cursors simultaneously but
this requires for introducing support from Lua.
 * Lua will loose precision if returned integer does not fit into double and this
requires for using cdata type
