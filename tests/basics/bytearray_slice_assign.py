try:
    bytearray()[:] = bytearray()
except TypeError:
    print("SKIP")
    import sys
    sys.exit()

# test slices; only 2 argument version supported by Micro Python at the moment
x = bytearray(range(10))

# Assignment
l = bytearray(x)
l[1:3] = bytearray([10, 20])
print(l)
l = bytearray(x)
l[1:3] = bytearray([10])
print(l)
l = bytearray(x)
l[1:3] = bytearray()
print(l)
l = bytearray(x)
#del l[1:3]
print(l)

l = bytearray(x)
l[:3] = bytearray([10, 20])
print(l)
l = bytearray(x)
l[:3] = bytearray()
print(l)
l = bytearray(x)
#del l[:3]
print(l)

l = bytearray(x)
l[:-3] = bytearray([10, 20])
print(l)
l = bytearray(x)
l[:-3] = bytearray()
print(l)
l = bytearray(x)
#del l[:-3]
print(l)
