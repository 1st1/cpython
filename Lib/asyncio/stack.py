"""Introspection utils for tasks call stacks."""

import sys
import types
import typing

from . import futures
from . import tasks

__all__ = (
    'capture_call_stack',
    'FrameCallStackEntry',
    'CoroutineCallStackEntry',
    'FutureCallStack',
)

# Sadly, we can't re-use the traceback's module datastructures as those
# are tailored for error reporting, whereas we need to represent an
# async call stack.
#
# Going with pretty verbose names as we'd like to export them to the
# top level asyncio namespace, and want to avoid future name clashes.


class FrameCallStackEntry(typing.NamedTuple):
    frame: types.FrameType


class CoroutineCallStackEntry(typing.NamedTuple):
    coroutine: types.CoroutineType


class FutureCallStack(typing.NamedTuple):
    future: futures.Future
    call_stack: list[FrameCallStackEntry | CoroutineCallStackEntry]
    awaited_by: list[FutureCallStack]


def _build_stack_for_future(future: any) -> FutureCallStack:
    if not isinstance(future, futures.Future):
        raise TypeError(
            f"{future!r} object does not appear to be compatible "
            f"with asyncio.Future"
        )

    try:
        get_coro = future.get_coro
    except AttributeError:
        coro = None
    else:
        coro = get_coro()

    st: list[CoroutineCallStackEntry] = []
    awaited_by: list[FutureCallStack] = []

    while coro is not None:
        if hasattr(coro, 'cr_await'):
            # A native coroutine or duck-type compatible iterator
            st.append(CoroutineCallStackEntry(coro))
            coro = coro.cr_await
        elif hasattr(coro, 'ag_await'):
            # A native async generator or duck-type compatible iterator
            st.append(CoroutineCallStackEntry(coro))
            coro = coro.ag_await
        else:
            break

    if fut_waiters := getattr(future, '_asyncio_awaited_by', None):
        for parent in fut_waiters:
            awaited_by.append(_build_stack_for_future(parent))

    st.reverse()
    return FutureCallStack(future, st, awaited_by)


def capture_call_stack(*, future: any = None) -> FutureCallStack | None:
    """Capture async call stack for the current task or the provided Future."""

    if future is not None:
        if future is not tasks.current_task():
            return _build_stack_for_future(future)
        # else: future is the current task, move on.
    else:
        future = tasks.current_task()

    if future is None:
        # This isn't a generic call stack introspection utility. If we
        # can't determine the current task and none was provided, we
        # just return.
        return None

    if not isinstance(future, futures.Future):
        raise TypeError(
            f"{future!r} object does not appear to be compatible "
            f"with asyncio.Future"
        )

    call_stack: list[FrameCallStackEntry | CoroutineCallStackEntry] = []

    f = sys._getframe(1)
    try:
        while f is not None:
            is_async = f.f_generator is not None

            if is_async:
                call_stack.append(CoroutineCallStackEntry(f.f_generator))
                if f.f_back is not None and f.f_back.f_generator is None:
                    # We've reached the bottom of the coroutine stack, which
                    # must be the Task that runs it.
                    break
            else:
                call_stack.append(FrameCallStackEntry(f))

            f = f.f_back
    finally:
        del f

    awaited_by = []
    if fut_waiters := getattr(future, '_asyncio_awaited_by', None):
        for parent in fut_waiters:
            awaited_by.append(_build_stack_for_future(parent))

    return FutureCallStack(future, call_stack, awaited_by)
