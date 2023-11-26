
for(outer in 0..15){
    var start_time = System.clock

    var total = 0
    for(i in 0..10000000) {
        total = total + i
    }

    var end_time = System.clock
    var duration_ms = (end_time - start_time) * 1000

    System.printAll(["10m .. range iterations took ", duration_ms, "ms ", "| ", total])
}