import time 
from math import sqrt

class Vec2:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def __add__(self, other):
        return Vec2(self.x + other.x, self.y + other.y)

    def distance(self, other):
        sx = self.x + other.x
        sy = self.y + other.y
        return sqrt(sx * sx + sy * sy)

for outer in range(10):
    start = time.time()

    for i in range(100000):
        a = Vec2(5, 5)
        b = Vec2(10, 10)
        c = a + b

    print("100k Vec2 create, create, add in ", (time.time() - start) * 1000, "ms")

for outer in range(10):
    start = time.time()

    a = Vec2(5, 5)
    b = Vec2(10, 10)
    for i in range(100000):
        c = a + b

    print("100k Vec2 add in ", (time.time() - start) * 1000, "ms")

for outer in range(10):
    start = time.time()

    a = Vec2(5, 5)
    b = Vec2(10, 10)
    for i in range(1000000):
        a.distance(b)

    print("1m Vec2 distance() in ", (time.time() - start) * 1000, "ms")