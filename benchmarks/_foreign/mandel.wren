

class Mandel {
    static norm2(re, im) {
        return re * re - im * (-im)
    }

    static abs(re, im) {
        return norm2(re, im).sqrt
    }

    static level(x, y) {
        var cre = x
        var cim = y
        var zre = cre
        var zim = cim

        for (i in 0..255) {
            var tre = zre * zre - zim * zim
            var tim = zre * zim + zim * zre

            zre = tre + cre
            zim = tim + cim

            if(abs(zre, zim) > 2) {
                return i
            }
        }    

        return 255
    }
}

for (outer in 0..15) {
    var start_time = System.clock

    var xmin = -2
    var xmax = 2
    var ymin = -2
    var ymax = 2
    var n = 256

    var dx = (xmax - xmin) / n
    var dy = (ymax - ymin) / n

    var result = 0
    for(xi in 0..n) {
        var x = xmin + xi * dx
        for(yi in 0..n) {
            var y = ymin + yi * dx
            result = result + Mandel.level(x, y)
        }
    }

    var duration_ms = (System.clock - start_time) * 1000
    System.printAll(["Finished in ", duration_ms, "ms ", "| ", result])
}