import time

def myrange(high):
    current = 0
    while current < high:
        yield current
        current += 1

for outer in range(15):
    start_time = time.time()

    for i in myrange(10000000):
        pass

    end_time = time.time()

    print("10m range() iterations in", (end_time - start_time) * 1000, "ms")