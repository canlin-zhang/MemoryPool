#include <vector>

struct alignas(64) ComplexStruct
{
    char x = 'X';
    std::vector<int> vec = {1, 2, 3, 4, 5};
    struct alignas(16) InnerStruct
    {
        int a = 42;
        double b = 3.14;
    } inner;
};
