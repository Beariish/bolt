import time
import math

def norm2(x_re, x_im):
    return x_re * x_re - x_im * (-x_im)

def im_abs(x_re, x_im):
    return math.sqrt(norm2(x_re, x_im))

def level(x, y):
    c_re = x
    c_im = y
    l = 0
    z_re = c_re 
    z_im = c_im
    
    while True:
        t_re = z_re * z_re - z_im * z_im
        t_im = z_re * z_im + z_im * z_re
        
        z_re = t_re + c_re
        z_im = t_im + c_im 

        l = l + 1

        if im_abs(z_re, z_im) > 2 or l > 255:
            return l - 1

for outer in range(15):
    start = time.time()

    xmin = -2.0
    xmax = 2.0
    ymin = -2.0
    ymax = 2.0
    n = 256 

    dx = (xmax - xmin) / n
    dy = (ymax - ymin) / n

    s = 0
    for i in range(n):
        x = xmin + i * dx
        for j in range(n):
            y = ymin + j * dy
            s = s + level(x, y)

    print("Time elapsed is", (time.time() - start) * 1000, "ms |", s)