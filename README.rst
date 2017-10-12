Fork of CPython with ``yield_in`` and ``yield_out`` parameters
for ``sys.set_asyncgen_hooks()``.

Example:

.. code:: python

    import asyncio
    import sys


    async def agen():
        i = 0
        while True:
            await asyncio.sleep(0.1)
            yield i
            i += 1
            if i > 10:
                break


    async def main():
        async for i in agen():
            print(f'just slept {i}')


    def yield_in(g):
        print('---- YIELD IN', g)


    def yield_out(g):
        print('---- YIELD OUT', g)


    sys.set_asyncgen_hooks(yield_in=yield_in, yield_out=yield_out)
    asyncio.get_event_loop().run_until_complete(main())


Copyright and License Information
---------------------------------

Copyright (c) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011,
2012, 2013, 2014, 2015, 2016, 2017 Python Software Foundation.  All rights
reserved.

Copyright (c) 2000 BeOpen.com.  All rights reserved.

Copyright (c) 1995-2001 Corporation for National Research Initiatives.  All
rights reserved.

Copyright (c) 1991-1995 Stichting Mathematisch Centrum.  All rights reserved.

See the file "LICENSE" for information on the history of this software, terms &
conditions for usage, and a DISCLAIMER OF ALL WARRANTIES.

This Python distribution contains *no* GNU General Public License (GPL) code,
so it may be used in proprietary projects.  There are interfaces to some GNU
code but these are entirely optional.

All trademarks referenced herein are property of their respective holders.
