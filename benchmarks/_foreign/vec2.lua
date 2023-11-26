local sqrt = math.sqrt

local Vec2 = {}
Vec2.__index = Vec2

local function make_vec2(x, y)
    return setmetatable({
        x = x, y = y
    }, Vec2)
end

Vec2.__add = function(self, other) 
    return make_vec2(self.x + other.x, self.y + other.y)
end

Vec2.distance = function(self, other)
    local sx = self.x + other.x
    local sy = self.y + other.y
    return sqrt(sx * sx + sy * sy)
end

for i = 1, 15 do
    local start = os.clock()

    for inner = 1, 100000 do
        local a = make_vec2(5, 5)
        local b = make_vec2(10, 10)
        local c = a + b
    end

    local duration_ms = (os.clock() - start) * 1000
    print("Finished 100k allocs in", duration_ms, "ms")
end

for i = 1, 15 do
    local start = os.clock()

    local a = make_vec2(5, 5)
    local b = make_vec2(10, 10)
    for inner = 1, 10000000 do
        local c = a + b
    end

    local duration_ms = (os.clock() - start) * 1000
    print("Finished 10m operators in", duration_ms, "ms")
end

for i = 1, 15 do
    local start = os.clock()

    local sum = 0
    local a = make_vec2(5, 5)
    local b = make_vec2(10, 10)
    for inner = 1, 1000000 do
        sum = sum + a:distance(b)
    end

    local duration_ms = (os.clock() - start) * 1000
    print("Finished 1m methods in", duration_ms, "ms", sum)
end

for i = 1, 15 do
    local start = os.clock()

    for inner = 1, 10000000 do
    end

    local duration_ms = (os.clock() - start) * 1000
    print("Finished 10m for in", duration_ms, "ms")
end
