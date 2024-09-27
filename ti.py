import sys
import _testexternalinspection as e
import pprint

print('sync:')
print(e.get_stack_trace(int(sys.argv[1])))

print('\n\nasync:')
pprint.pp(e.get_async_stack_trace(int(sys.argv[1])))
