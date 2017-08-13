import sys


class ContextStack:

    def __init__(self, key):
        self.key = key

    def _get_level(self):
        level_key = type(self), self.key
        return sys.get_execution_context_item(level_key, 0)

    def _set_level(self, level):
        level_key = type(self), self.key
        sys.set_execution_context_item(level_key, level)

    def __enter__(self):
        level = self._get_level()
        self._set_level(level + 1)

    def __exit__(self, *ex):
        level = self._get_level()
        self._set_level(level - 1)

    def get_value(self, default=None, level=None):
        if level is None:
            level = self._get_level()

        value_key = type(self), self.key, level
        return sys.get_execution_context_item(value_key, default)

    def set_value(self, value):
        level = self._get_level()
        value_key = type(self), self.key, level
        return sys.set_execution_context_item(value_key, value)

    def get_stack(self):
        stack = []
        level = self._get_level()
        while level:
            stack.append(self.get_value(level=level))
            level -= 1
        return list(reversed(stack))


stack = ContextStack('mystack')


def gen2():
    with stack:
        stack.set_value("generator-2!")
        while True:
            print(stack.get_stack())
            yield


def gen():
    with stack:
        stack.set_value("generator!")
        while True:
            print(stack.get_stack())
            next(g2)
            yield


g2 = gen2()


with stack:
    stack.set_value("round 1")
    g = gen()
    next(g)

with stack:
    stack.set_value("round 2")
    next(g)
