This is a fork of CPython that implement an immutable mapping using
Hash array mapped trie (HAMT) for PEP 550.

Two files of interest: Objects/hamtobject.c and Objects/hamtobject.h.

To test the new datastructure use the ``hamt()`` builtin::

    h = hamt()
    h.set('a', 'b')
    print(h.get('a'))

Only ``set()`` and ``get()`` methods are implemented.
Read more about this in the PEP.

