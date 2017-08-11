Reference PEP 550 Implementation
--------------------------------

This is a fork of CPython that implements most parts of PEP 550.

The following APIs are implemented:

* ``sys.get_execution_context()``

* ``sys.set_execution_context()``

* ``sys.set_execution_context_item()``

* ``sys.get_execution_context_item()``

* ``sys.ExecutionContext``:

  * ``[key]``

  * ``[key] = value``

  * ``.run()``

Refer to the PEP for more details.


Copyright and License Information
---------------------------------

Copyright (c) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011,
2012, 2013, 2014, 2015, 2016, 2017 Python Software Foundation.
All rights reserved.

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
