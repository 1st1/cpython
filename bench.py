import time

I = 1_000_000
GETS = 1000
SETS = 100
KEY = '5'


def val():
    return sum(range(100))


for N in [5, 10, 20, 30, 100, 200, 300, 500, 1000]:
    print('=============')
    print(f'  # of items: {N}; iterations: {I}; get() x {GETS}; set() x {SETS}')
    print()


    h = hamt()
    d = dict()

    for i in range(N):
        h = h.set(str(i), i)
        d[str(i)] = i

    assert len(h) == len(d) == N

    for _ in range(I):
        assert h.get(KEY) == d.get(KEY)


    st = time.monotonic()
    for _ in range(I):
        d.get(KEY)
        d.get(KEY)
        d.get(KEY)
        d.get(KEY)
        d.get(KEY)

        d.get(KEY)
        d.get(KEY)
        d.get(KEY)
        d.get(KEY)
        d.get(KEY)

        d2 = d.copy()
        d2['aaa'] = 123

    end = time.monotonic() - st
    print(f"  dict copy: \t\t\t{end:.4f}s")


    st = time.monotonic()
    for _ in range(I):
        d.get(KEY)
        d.get(KEY)
        d.get(KEY)
        d.get(KEY)
        d.get(KEY)

        d.get(KEY)
        d.get(KEY)
        d.get(KEY)
        d.get(KEY)
        d.get(KEY)

        d['aaa'] = 123

    end = time.monotonic() - st
    print(f"  dict copy_on_write: \t\t{end:.4f}s")


    st = time.monotonic()
    for _ in range(I):
        h.get(KEY)
        h.get(KEY)
        h.get(KEY)
        h.get(KEY)
        h.get(KEY)

        h.get(KEY)
        h.get(KEY)
        h.get(KEY)
        h.get(KEY)
        h.get(KEY)

        h2 = h.set('aaa', 123)

    end = time.monotonic() - st
    print(f"  hamt:\t\t\t\t{end:.4f}s")
