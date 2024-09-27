import asyncio
import time
import os

print(os.getpid())

async def deep():
    print(asyncio.capture_call_stack())
    time.sleep(10000)

async def c1():
    await asyncio.sleep(0)
    await deep()

async def c2():
    await asyncio.sleep(0)

async def main():
    await asyncio.gather(c1(), c2())

asyncio.run(main())
