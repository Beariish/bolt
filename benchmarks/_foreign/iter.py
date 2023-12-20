import time

def myrange(high):
    current = 0
    while current < high:
        yield current
        current += 1

def fib(n):
    a = 0.0
    b = 1.0
    for i in range(n):
        (a, b) = (b, a + b)
    return a

for outer in range(15):
    start_time = time.time()

    fib(10000000)

    end_time = time.time()

    print("10m range() iterations in", (end_time - start_time) * 1000, "ms")

