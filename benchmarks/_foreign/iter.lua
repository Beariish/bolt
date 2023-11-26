local init = os.clock()

local function range_iter(max, i)
    i = i + 1
    if i < max then return i end
    return nil
end

local function range_stateless(max)
    return range_iter, max, -1
end

local function range(max)
    local i = -1

    return function()
        i = i + 1
        if i < max then return i end
        return nil
    end
end

for i = 1, 15 do
    local start = os.clock()

    for i = 0, 10000000 do
        -- ...
    end

    local duration_ms = (os.clock() - start) * 1000
    print("Finished 10m numfor in", duration_ms, "ms")
end

for i = 1, 15 do
    local start = os.clock()

    for i in range_stateless(10000000) do
        -- ...
    end

    local duration_ms = (os.clock() - start) * 1000
    print("Finished 10m stateless range in", duration_ms, "ms")
end

for i = 1, 15 do
    local start = os.clock()

    for i in range(10000000) do
        -- ...
    end

    local duration_ms = (os.clock() - start) * 1000
    print("Finished 10m range in", duration_ms, "ms")
end

-- local i = 100000000
-- local x = 0
-- while x < i do
--     x = x + 1
-- end


print("done", (os.clock() - init) * 1000)