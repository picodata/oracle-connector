-- init.lua (internal file)

local fiber = require('fiber')
local driver = require('ora.driver')
local ffi = require('ffi')

local pool_mt
local conn_mt

--create a new connection
local function conn_create(ora_conn, raise)
    local queue = fiber.channel(1)
    queue:put(true)
    local conn = setmetatable({
        usable = true,
        conn = ora_conn,
        queue = queue,
        raise = raise,
    }, conn_mt)

    return conn
end

-- get connection from pool
local function conn_get(pool)
    local ora_conn = pool.queue:get()
    local status
    if ora_conn == nil then
        status, ora_conn = driver.connect(pool.conn_string, pool.user, pool.pass)
        if status < 0 then
            return error(ora_conn)
        end
    end
    local conn = conn_create(ora_conn, pool.raise)
    conn.__gc_hook = ffi.gc(ffi.new('void *'),
        function(self)
            pool.queue:put(ora_conn)
        end)
    return conn
end

local function conn_put(conn)
    local oraconn = conn.conn
    ffi.gc(conn.__gc_hook, nil)
    if not conn.queue:get() then
        conn.usable = false
        conn:close()
        return nil
    end
    conn.usable = false
    return oraconn
end

conn_mt = {
    __index = {
        execute = function(self, sql, args)
            if not self.usable then
                if self.raise then
                    return error('Connection is not usable')
                end
                return nil, nil, false, 'Connection is not usable'
            end
            if not self.queue:get() then
                self.queue:put(false)
                if self.raise then
                    return error('Connection is broken')
                end
                return nil, nil, false, 'Connection is broken'
            end
            local status, msg, data, output = self.conn:execute(sql, args or {})
            if status ~= 0 then
                self.queue:put(status > 0)
                if self.raise then
                    return error(msg)
                end
                return nil, nil, false, msg
            end
            self.queue:put(true)
            return data, output, true, msg
        end,
        cursor_open = function(self, sql, args)
            if not self.usable then
                if self.raise then
                    return error('Connection is not usable')
                end
                return false, 'Connection is not usable'
            end
            if not self.queue:get() then
                self.queue:put(false)
                if self.raise then
                    return error('Connection is broken')
                end
                return false, 'Connection is broken'
            end
            local status, msg = self.conn:cursor_open(sql, args or {})
            if status ~= 0 then
                self.queue:put(status > 0)
                if self.raise then
                    return error(msg)
                end
                return false, msg
            end
            self.queue:put(true)
            return true, msg
        end,
        cursor_fetch = function(self)
            if not self.usable then
                if self.raise then
                    return error('Connection is not usable')
                end
                return nil, false, 'Connection is not usable'
            end
            if not self.queue:get() then
                self.queue:put(false)
                if self.raise then
                    return error('Connection is broken')
                end
                return nil, false, 'Connection is broken'
            end
            local status, msg, data = self.conn:cursor_fetch()
            if status ~= 0 then
                self.queue:put(status > 0)
                if self.raise then
                    return error(msg)
                end
                return nil, false, msg
            end
            self.queue:put(true)
            return data, true, msg
        end,
        cursor_close = function(self)
            if not self.usable then
                if self.raise then
                    return error('Connection is not usable')
                end
                return 'Connection is not usable'
            end
            if not self.queue:get() then
                self.queue:put(false)
                if self.raise then
                    return error('Connection is broken')
                end
                return false, 'Connection is broken'
            end
            self.conn:cursor_close()
            self.queue:put(true)
            return true
        end,
        begin = function(self)
            if not self.usable then
                if self.raise then
                    return error('Connection is not usable')
                end
                return false, 'Connection is not usable'
            end
            if not self.queue:get() then
                self.queue:put(false)
                if self.raise then
                    return error('Connection is broken')
                end
                return false, 'Connection is broken'
            end
            self.queue:put(true)
            return true
        end,
        commit = function(self)
            local _, _, ret, msg = self:execute('COMMIT')
            return ret, msg
        end,
        rollback = function(self)
            _, _, ret, msg = self:execute('ROLLBACK')
            return ret, msg
        end,
        ping = function(self)
            local status, data, msg = pcall(self.execute, self, 'SELECT 1 AS code FROM dual')
            return status and data[1]["CODE"] == 1
        end,
        close = function(self)
            if not self.usable then
                if self.raise then
                    return error('Connection is not usable')
                end
                return false, 'Connection is not usable'
            end
            if not self.queue:get() then
                self.queue:put(false)
                if self.raise then
                    return error('Connection is broken')
                end
                return false, 'Connection is broken'
            end
            self.usable = false
            self.conn:close()
            self.queue:put(false)
            return true
        end
    }
}

local function build_conn_string(opts)
    return string.format("%s:%s/%s", opts.host, opts.port, opts.db), opts.user, opts.pass
end

-- Create connection pool. Accepts ora connection params (host, port, user,
-- password, dbname) and size.
local function pool_create(opts)
    opts = opts or {}
    local conn_string, user, pass = build_conn_string(opts)
    opts.size = opts.size or 1
    local queue = fiber.channel(opts.size)

    for i = 1, opts.size do
        local status, conn = driver.connect(conn_string, user, pass)
        if status < 0 then
            while queue:count() > 0 do
                local ora_conn = queue:get()
                ora_conn:close()
            end
            if status < 0 then
                return error(conn)
            end
        end
        queue:put(conn)
    end

    return setmetatable({
        -- connection variables
        host        = opts.host,
        port        = opts.port,
        user        = opts.user,
        pass        = opts.pass,
        db          = opts.db,
        size        = opts.size,
        conn_string  = conn_string,

        -- private variables
        queue       = queue,
        usable      = true,
        raise       = opts.raise or false,
    }, pool_mt)
end

-- Close pool
local function pool_close(self)
    self.usable = false
    while self.queue:count() > 0 do
        local ora_conn = self.queue:get()
        if ora_conn ~= nil then
            ora_conn:close()
        end
    end
end

-- Returns connection
local function pool_get(self)
    if not self.usable then
        return error('Pool is not usable')
    end
    return conn_get(self)
end

-- Free binded connection
local function pool_put(self, conn)
    if not self.usable then
        return error('Pool is not usable')
    end
    self.queue:put(conn_put(conn))
end

pool_mt = {
    __index = {
        get = pool_get;
        put = pool_put;
        close = pool_close;
    }
}

-- Create connection. Accepts ora connection params (host, port, user,
-- password, dbname) separatelly or in one string and raise flag.
local function connect(opts)
    opts = opts or {}

    local conn_string, user, pass = build_conn_string(opts)
    local status, ora_conn = driver.connect(conn_string, user, pass)
    if status < 0 then
        return error(ora_conn)
    end
    return conn_create(ora_conn, opts.raise or false)
end

return {
    connect = connect;
    pool_create = pool_create;
}
