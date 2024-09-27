import sys
import _testexternalinspection as e
print(e.get_stack_trace(int(sys.argv[1])))
