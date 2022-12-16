#!/usr/bin/env tarantool
local tap = require('tap')

local ora = require('ora')
local fiber = require('fiber')
local os = require('os')

local db_server = os.environ()['DBSERVER']
if db_server == nil then
    db_server = 'localhost'
end

local db_port = os.environ()['DBPORT']
if db_port == nil then
    db_port = '1521'
end

local db_address = string.format("%s:%s", db_server, db_port)
print("Running tests to '"..db_address.."'")

local conn = ora.connect({ host = db_server, port = tostring(db_port), user = 'SYSTEM', pass = 'tntPswd', db = 'tnt', raise = true })
local conn_no_raise = ora.connect({ host = db_server, port = tostring(db_port), user = 'SYSTEM', pass = 'tntPswd', db = 'tnt', raise = false })

local function test_smoke(t, c)
    t:plan(41)
    t:ok(c ~= nil, "connection")

    local _, _, ok = conn:execute("create table test1 (id number not null, name varchar2(40), CONSTRAINT pk_test1 PRIMARY KEY(id))")
    t:ok(ok)
    local _, _, ok = conn:execute("insert into test1 values (:ID, :NAME)", {['ID'] = 1, ['NAME'] = "one"})
    t:ok(ok)
    local _, _, ok = conn:execute("insert into test1 values (:ID, :NAME)", {['ID'] = 2, ['NAME'] = {}}) --NAME == NULL
    t:ok(ok)
    local _, _, ok = conn:execute("insert into test1 values (:ID, :NAME)", {['ID'] = 3, ['NAME'] = {['value'] = 'three'}})
    t:ok(ok)

    t:is_deeply(
        {c:execute("SELECT * FROM test1 ORDER BY ID", {})},
        {{{ID = 1, NAME = 'one'}, {ID = 2, NAME = ""}, { ID = 3, NAME = 'three'}}, nil, true},
        "check insert correctness"
    )

    local _, output, ok = conn:execute("update test1 set NAME = upper(NAME) where ID != :ID returning NAME into :NAME", {['ID'] = 2, ['NAME'] = {['type'] = 'string', ['size'] = 40}})
    t:ok(ok, "returning ok")
    t:is_deeply(output, {['NAME'] = {[0] = "ONE", [1] = "THREE"}}, "returning values")

    local ok = conn:cursor_open("select * from test1 order by ID")
    t:ok(ok, "cursor open")
    local data, ok = conn:cursor_fetch()
    t:ok(ok, "fetch")
    t:is_deeply(data, {['ID'] = 1, ['NAME'] = 'ONE'}, "data")
    local data, ok = conn:cursor_fetch()
    t:ok(ok, "fetch")
    t:is_deeply(data, {['ID'] = 2, ['NAME'] = ''}, "data")
    local ok = conn:cursor_close()
    t:ok(ok)

    local ok = conn:cursor_open("select * from test1 where ID = :ID order by ID", {['ID'] = 3})
    t:ok(ok, "cursor open with parameter")
    local data, ok = conn:cursor_fetch()
    t:ok(ok, "fetch")
    t:is_deeply(data, {['ID'] = 3, ['NAME'] = 'THREE'}, "data")
    local data, ok = conn:cursor_fetch()
    t:ok(ok, "fetch last")
    t:is(data, nil, "fetch last data")
    local result, msg = pcall(conn.cursor_fetch, conn)
    t:is(msg, "there is no open cursor", "fetch from closed cursor")

    local result, msg = pcall(conn.execute, conn, "insert into test1 values (:ID, :NAME)")
    t:is(msg, "code 1008, message ORA-01008: not all variables bound\n", "not all variables bound")

    local result, msg = pcall(conn.execute, conn, "insert into test1 values (:ID, :NAME)", {['NAME'] = "one"})
    t:is(msg, "code 1008, message ORA-01008: not all variables bound\n", "not all variables bound 2")

    local result, msg = pcall(conn.execute, conn, "insert into test1 values (:ID, :NAME)", {['ID'] = 1, ['NAME'] = "one", ['BAD'] = 4})
    t:is(msg, "code 1036, message ORA-01036: illegal variable name/number\n", "Failed test case: invalid variable name/number")

    local result, msg = pcall(conn.execute, conn, "insert into test2 values (:ID, :NAME)", {['ID'] = 1, ['NAME'] = "one"})
    t:is(msg, "code 942, message ORA-00942: table or view does not exist\n", "Failed test case: table does not exists")

    local result, msg = pcall(conn.execute, conn, "wrong SQL statement")
    t:is(msg, "code 900, message ORA-00900: invalid SQL statement\n", "Failed test case: invalid SQL statement")

    local result, msg = pcall(conn.execute, conn, "insert into test1 values (:ID, :NAME)", {['ID'] = 1, ['NAME'] = "one"})
    t:is(msg, "code 1, message ORA-00001: unique constraint (SYSTEM.PK_TEST1) violated\n", "Failed test case: unique constraint violation")

    local result, msg = pcall(conn.execute, conn, "select ASF from test1")
    t:is(msg, "code 904, message ORA-00904: \"ASF\": invalid identifier\n", "Failed test case: invalid identifier")

    local _, _, ok = conn:execute("begin insert into test1 values (:ID, :TEXT); commit; end;", {['ID'] = 5, ['TEXT'] = "five"})
    t:ok(ok, "dml inside PLSQL with arguments")

    local _, output, ok = conn:execute("begin select count(*) into :COUNT from test1; end;", {['count'] = {['type'] = 'number'}})
    t:ok(ok, "PLSQL with output value")
    t:is_deeply(output, {['count'] = {[0] = 4}})

    _, _, ok, msg = conn:execute([[CREATE OR REPLACE PROCEDURE P AS
    e EXCEPTION;
BEGIN
    RAISE e;
END]], {})
    t:ok(ok, "PLSQL - SUCCESS WITH INFO")
    t:is(msg, "ORA-24344: success with compilation error\n", "Get info from SUCCESS WITH INFO")


    _, _, ok, msg = pcall(conn.execute, conn, [[DECLARE
    e_user_exc EXCEPTION;
    PRAGMA exception_init( e_user_exc, -20001 );
BEGIN
    RAISE e_user_exc;
END;]])

    local data, output, ok, msg = conn_no_raise:execute("insert into test2 values (:ID, :NAME)", {['ID'] = 1, ['NAME'] = "one"})
    t:is(data, nil, "Failed nil data")
    t:is(output, nil, "Failed nil output")
    t:is(ok, false, "Failed false status")
    t:is(msg, "code 942, message ORA-00942: table or view does not exist\n", "Failed error msg")

    local done = false
    local fSleep = fiber.create(function() conn:execute("BEGIN\n  DBMS_LOCK.SLEEP(1);\nEND;") done = true end)
    conn:execute("select 1 from dual")
    t:ok(done, "statement execution should not start until connection is busy")

    local p = ora.pool_create({ host = db_server, port = tostring(db_port), user = 'SYSTEM', pass = 'tntPswd', db = 'tnt', raise = true, size = 1})
    local f1 = fiber.new(function (pool)
        local c1 = p:get()
        c1:execute("select 1 from dual")
        c1 = nil
    end, p)
    f1:set_joinable(true)
    f1:join()

    collectgarbage('collect')
    local c2 = p:get()
    data, output, ok, msg = c2:execute("select 1 as ID from dual")
    t:is_deeply(data, {{['ID'] = 1}}, "recycled connection")
    t:is(output, nil, "recycled connection")
    t:is(ok, true, "recycled connection")
    t:is(msg, nil, "recycled connection")

end

local test = tap.test('oracle-connector')
test:plan(2)

pcall(function() conn:execute('drop table test1') end)
pcall(function() conn:execute('drop table test2') end)
test_smoke(test, conn)
pcall(function() conn:execute('drop table test1') end)
pcall(function() conn:execute('drop table test2') end)

conn:close()
conn = nil

os.exit(test:check() == true and 0 or -1)
