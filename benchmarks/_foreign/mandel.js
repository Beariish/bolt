function norm2(re, im) {
    return re * re - im * (-im)
}

function xabs(re, im) {
    return Math.sqrt(norm2(re, im))
}

function level(x, y) {
    let cre = x
    let cim = y
    let zre = cre
    let zim = cim

    for (let i = 0; i < 255; i++) {
        let tre = zre * zre - zim * zim
        let tim = zre * zim + zim * zre

        zre = tre + cre
        zim = tim + cim

        if(xabs(zre, zim) > 2) {
            return i
        }
    }

    return 255
}

for (let outer = 0; outer < 15; outer++) {
    var start_time = Date.now()

    let xmin = -2
    let xmax = 2
    let ymin = -2
    let ymax = 2
    let n = 256

    let dx = 4 / n
    let dy = 4 / n

    let result = 0
    for(let xi = 0; xi < n; xi++) {
        let x = xmin + xi * dx
        for(let yi = 0; yi < n; yi++) {
            let y = ymin + yi * dy
            result += level(x, y)
        }
    }

    var end_time = Date.now()
    var duration_ms = end_time - start_time
    console.log("10m range() iterations took", duration_ms, "ms", "|", result)
}

