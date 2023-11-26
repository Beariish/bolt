class Vec2 {
    x { _x }
    y { _y } 

    construct new(x, y) {
        _x = x
        _y = y
    }

    +(other) {
        return Vec2.new(_x + other.x, _y + other.y)
    }

    distance(other) {
        var sx = _x + other.x
        var sy = _y + other.y
        return (sx * sx + sy * sy).sqrt
    }
}

for(outer in 0..10){
    var start_time = System.clock

    for(i in 0..100000) {
        var a = Vec2.new(5, 5)
        var b = Vec2.new(10, 10)
        var c = a + b
    }

    var end_time = System.clock
    var duration_ms = (end_time - start_time) * 1000

    System.printAll(["100k vec2 iterations took ", duration_ms, "ms "])
}

for(outer in 0..10){
    var start_time = System.clock

    var a = Vec2.new(5, 5)
    var b = Vec2.new(10, 10)
    for(i in 0..100000) {
        var c = a + b
    }

    var end_time = System.clock
    var duration_ms = (end_time - start_time) * 1000

    System.printAll(["100k vec2 operators took ", duration_ms, "ms "])
}

for(outer in 0..10){
    var start_time = System.clock

    var a = Vec2.new(5, 5)
    var b = Vec2.new(10, 10)
    for(i in 0..1000000) {
        var c = a.distance(b)
    }

    var end_time = System.clock
    var duration_ms = (end_time - start_time) * 1000

    System.printAll(["1m vec2 methods took ", duration_ms, "ms "])
}