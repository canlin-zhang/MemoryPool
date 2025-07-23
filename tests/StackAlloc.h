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
 * A template class that implements a simple stack structure.
 * This demostrates how to use alloctor_traits (specifically with MemoryPool)
 */

#pragma once

#include <memory>
#include <stdexcept>

template <typename T>
struct StackNode_
{
    T data;
    StackNode_* prev;
};

template <class T, class Alloc = std::allocator<T>>
class StackAlloc
{

  public:
    using Node = StackNode_<T>;
    using allocator = typename std::allocator_traits<Alloc>::template rebind_alloc<Node>;
    using allocator_traits = std::allocator_traits<allocator>;

    StackAlloc() = default;
    ~StackAlloc()
    {
        clear();
    }

    bool empty() const
    {
        return head_ == nullptr;
    }

    void clear()
    {
        Node* curr = head_;
        while (curr)
        {
            Node* tmp = curr->prev;
            allocator_traits::destroy(allocator_, curr);
            allocator_.deallocate(curr, 1);
            curr = tmp;
        }
        head_ = nullptr;
    }

    void push(T element)
    {
        Node* newNode = allocator_.allocate(1);
        allocator_traits::construct(allocator_, newNode); // value-init
        newNode->data = element;
        newNode->prev = head_;
        head_ = newNode;
    }

    T pop()
    {

        T result = head_->data;
        Node* tmp = head_->prev;
        allocator_traits::destroy(allocator_, head_);
        allocator_.deallocate(head_, 1);
        head_ = tmp;
        return result;
    }

    T top() const
    {
        return head_->data;
    }

  private:
    allocator allocator_;
    Node* head_ = nullptr;
};