import sys
import _testexternalinspection as e
import pprint

print('sync:')
print(e.get_stack_trace(int(sys.argv[1])))

print('\n\nasync:')
pid = int(sys.argv[1])

pprint.pp(e.get_async_stack_trace(pid))


print()
print()

import time
started = time.monotonic_ns()
N = 200
for _ in range(N):
    e.get_async_stack_trace(pid)
print(f'iterations={N}', f'{(time.monotonic_ns() - started) / N}ns')
