class Vec2 {
    x; y;

    constructor(x, y) {
        this.x = x;
        this.y = y;
    }

    add(other) {
        return new Vec2(this.x + other.x, this.y + other.y)
    }

    distance(other) {
        var sx = this.x + other.x
        var sy = this.y + other.y
        return Math.sqrt(sx * sx + sy * sy)
    }
}

for(let i = 0; i < 15; i++) {
    var start_time = Date.now()

    for(let j = 0; j < 100000; j++) {
        var a = new Vec2(5, 5);
        var b = new Vec2(10, 10);
        var c = a.add(b);
    }

    var end_time = Date.now()
    var duration_ms = end_time - start_time
    console.log("100k Vec2 create, create, add iterations took", duration_ms, "ms")
}

for(let i = 0; i < 15; i++) {
    var start_time = Date.now()

    var a = new Vec2(5, 5);
    var b = new Vec2(10, 10);
    for(let j = 0; j < 100000; j++) {
        var c = a.add(b);
    }

    var end_time = Date.now()
    var duration_ms = end_time - start_time
    console.log("100k Vec2 add iterations took", duration_ms, "ms")
}

for(let i = 0; i < 15; i++) {
    var start_time = Date.now()

    var a = new Vec2(5, 5);
    var b = new Vec2(10, 10);
    var c = 0
    for(let j = 0; j < 1000000; j++) {
        c += a.distance(b);
    }

    var end_time = Date.now()
    var duration_ms = end_time - start_time
    console.log("1m Vec2 distance() took", duration_ms, "ms", c)
}