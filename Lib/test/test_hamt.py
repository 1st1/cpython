import unittest


class HashKey:
    def __init__(self, hash, name):
        assert hash != -1
        self.name = name
        self.hash = hash

    def __repr__(self):
        return f'<Key name:{self.name} hash:{self.hash}>'

    def __hash__(self):
        return self.hash

    def __eq__(self, other):
        if not isinstance(other, HashKey):
            return NotImplemented
        return self.name == other.name


class HamtTest(unittest.TestCase):

    def test_hashkey_helper_1(self):
        k1 = HashKey(10, 'aaa')
        k2 = HashKey(10, 'bbb')

        self.assertNotEqual(k1, k2)
        self.assertEqual(hash(k1), hash(k2))

        d = dict()
        d[k1] = 'a'
        d[k2] = 'b'

        self.assertEqual(d[k1], 'a')
        self.assertEqual(d[k2], 'b')

    def test_hamt_basics_1(self):
        h = hamt()
        h = None  # NoQA

    def test_hamt_basics_2(self):
        h = hamt()
        self.assertEqual(len(h), 0)

        h2 = h.set('a', 'b')
        self.assertIsNot(h, h2)
        self.assertEqual(len(h), 0)
        self.assertEqual(len(h2), 1)

        self.assertIsNone(h.get('a'))
        self.assertEqual(h.get('a', 42), 42)

        self.assertEqual(h2.get('a'), 'b')

        h3 = h2.set('b', 10)
        self.assertIsNot(h2, h3)
        self.assertEqual(len(h), 0)
        self.assertEqual(len(h2), 1)
        self.assertEqual(len(h3), 2)
        self.assertEqual(h3.get('a'), 'b')
        self.assertEqual(h3.get('b'), 10)

        self.assertIsNone(h.get('b'))
        self.assertIsNone(h2.get('b'))

        self.assertIsNone(h.get('a'))
        self.assertEqual(h2.get('a'), 'b')

        h = h2 = h3 = None

    def test_hamt_basics_3(self):
        h = hamt()
        o = object()
        h.set('1', o).set('1', o)

    def test_hamt_collision_1(self):
        k1 = HashKey(10, 'aaa')
        k2 = HashKey(10, 'bbb')
        k3 = HashKey(10, 'ccc')

        h = hamt()
        h2 = h.set(k1, 'a')
        h3 = h2.set(k2, 'b')

        self.assertEqual(h.get(k1), None)
        self.assertEqual(h.get(k2), None)

        self.assertEqual(h2.get(k1), 'a')
        self.assertEqual(h2.get(k2), None)

        self.assertEqual(h3.get(k1), 'a')
        self.assertEqual(h3.get(k2), 'b')

        h4 = h3.set(k2, 'cc')
        h5 = h4.set(k3, 'aa')

        self.assertEqual(h3.get(k1), 'a')
        self.assertEqual(h3.get(k2), 'b')
        self.assertEqual(h4.get(k1), 'a')
        self.assertEqual(h4.get(k2), 'cc')
        self.assertEqual(h4.get(k3), None)
        self.assertEqual(h5.get(k1), 'a')
        self.assertEqual(h5.get(k2), 'cc')
        self.assertEqual(h5.get(k2), 'cc')
        self.assertEqual(h5.get(k3), 'aa')

        self.assertEqual(len(h), 0)
        self.assertEqual(len(h2), 1)
        self.assertEqual(len(h3), 2)
        self.assertEqual(len(h4), 2)
        self.assertEqual(len(h5), 3)

    def test_hamt_stress_1(self):
        for _ in range(5):
            h = hamt()
            d = dict()
            N = 10000
            for i in range(N):
                h = h.set(str(i), i)
                d[str(i)] = i
            self.assertEqual(len(h), N)
            for i in range(N):
                self.assertEqual(h.get(str(i), 'not found'), i)


if __name__ == "__main__":
    unittest.main()
