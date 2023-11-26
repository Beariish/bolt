
function norm2(re, im)
    return re * re - im * (-im)
end

function abs(re, im)
    return math.sqrt(norm2(re, im))
end

function level(x, y)
    local cre = x
    local cim = y
    local zre = cre
    local zim = cim

    for i = 0, 255 do
        local tre = zre * zre - zim * zim
        local tim = zre * zim + zim * zre

        zre = tre + cre
        zim = tim + cim

        if abs(zre, zim) > 2 then
            return i
        end
    end

    return 255
end

for i = 0, 14 do
    local start = os.clock()

    local xmin = -2
    local xmax = 2
    local ymin = -2
    local ymax = 2
    local n = 256

    local dx = (xmax - xmin) / n
    local dy = (ymax - ymin) / n

    local result = 0
    for iterx = 0, n - 1 do
        local x = xmin + iterx * dx
        for itery = 0, n - 1 do
            local y = ymin + itery * dy
            result = result + level(x, y)
        end
    end

    local duration_ms = (os.clock() - start) * 1000
    print("Finished in", duration_ms, "ms", "|", result)
end

