
function* range(max) {
    for(let i = 0; i < max; ++i) {
        yield i;
    }
}

for(let outer of range(15)) {
    var start_time = Date.now()

    for(let i of range(10000000)) {
    }

    var end_time = Date.now()
    var duration_ms = end_time - start_time
    console.log("10m range() iterations took", duration_ms, "ms")
}
