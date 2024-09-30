import asyncio
import time
import os

import pprint
import test.test_asyncio.test_stack as ts

print(os.getpid())

def c5():
    # pprint.pp(ts.capture_test_stack())
    # await asyncio.sleep(2)
    pass

async def c4():
    await asyncio.sleep(5)
    c5()

async def c3():
    await c4()

async def c2():
    await c3()

async def c1(task):
    await asyncio.sleep(1)
    asyncio.print_call_stack(future=task)
    await task

async def main():
    async with asyncio.TaskGroup() as tg:
        task = tg.create_task(c2(), name="c2_root")
        tg.create_task(c1(task), name="sub_main_1")
        tg.create_task(c1(task), name="sub_main_2")

asyncio.run(main())
