/*-
 * Copyright (c) 2013 Cosku Acay, http://www.coskuacay.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*-
 * Provided to compare the default allocator to MemoryPool
 *
 * Check out StackAlloc.h for a stack implementation that takes an allocator as
 * a template argument. This may give you some idea about how to use MemoryPool.
 *
 * This code basically creates two stacks: one using the default allocator and
 * one using the MemoryPool allocator. It pushes a bunch of objects in them and
 * then pops them out. We repeat the process several times and time how long
 * this takes for each of the stacks.
 *
 * Do not forget to turn on optimizations (use -O2 or -O3 for GCC). This is a
 * benchmark, we want inlined code.
 */

#include <pool_allocator/pool_allocator.h>
#include "StackAlloc.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <vector>
#include <chrono>

class MemoryPoolTest : public ::testing::Test
{
protected:
    static const int ELEMS = 1000000;
    static const int REPS = 50;
    static int64_t pool_time;
    static int64_t default_time;
};

int64_t MemoryPoolTest::pool_time = 0;
int64_t MemoryPoolTest::default_time = 0;

TEST_F(MemoryPoolTest, default_allocator_perf)
{

    std::cout << "Copyright (c) 2013 Cosku Acay, http://www.coskuacay.com\n";
    std::cout << "Provided to compare the default allocator to MemoryPool.\n\n";

    auto start = std::chrono::high_resolution_clock::now();

    /* Use the default allocator */
    StackAlloc<int, std::allocator<int>> stackDefault;
    for (int j = 0; j < REPS; j++)
    {
        assert(stackDefault.empty());
        for (int i = 0; i < ELEMS / 4; i++)
        {
            // Unroll to time the actual code and not the loop
            stackDefault.push(i);
            stackDefault.push(i);
            stackDefault.push(i);
            stackDefault.push(i);
        }
        for (int i = 0; i < ELEMS / 4; i++)
        {
            // Unroll to time the actual code and not the loop
            stackDefault.pop();
            stackDefault.pop();
            stackDefault.pop();
            stackDefault.pop();
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    MemoryPoolTest::default_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    printf("Default Allocator: %ld ms\n", MemoryPoolTest::default_time);
    SUCCEED();
}
