#pragma once
// table.sort exactly as LuaJIT implements it (lib_table.c auxsort, the Lua 5.1 quicksort). It is
// not stable, so comparators with ties (cards sorted by rank, nominal, x position...) order equal
// elements in this algorithm's specific way; matching it keeps card orders identical.
#include <utility>
#include <vector>

namespace vl {

namespace detail {
template <class T, class Less>
void lua_auxsort(std::vector<T> &a, int l, int u, Less &lt) {
    // indices are 1-based like the Lua array
    auto A = [&](int i) -> T & { return a[i - 1]; };
    while (l < u) {
        if (lt(A(u), A(l))) std::swap(A(l), A(u));
        if (u - l == 1) break;
        int i = (l + u) / 2;
        if (lt(A(i), A(l))) {
            std::swap(A(i), A(l));
        } else if (lt(A(u), A(i))) {
            std::swap(A(i), A(u));
        }
        if (u - l == 2) break;
        T P = A(i);
        std::swap(A(i), A(u - 1));  // a[i] = a[u-1], a[u-1] = P
        int j = u - 1;
        i = l;
        for (;;) {
            // (Lua raises "invalid order function for sorting" where these return)
            for (;;) {
                if (++i > (int)a.size()) return;
                if (!lt(A(i), P)) break;
                if (i > u) return;
            }
            for (;;) {
                if (--j < 1) return;
                if (!lt(P, A(j))) break;
                if (j < l) return;
            }
            if (j < i) break;
            std::swap(A(i), A(j));
        }
        std::swap(A(u - 1), A(i));
        if (i - l < u - i) {
            j = l;
            i = i - 1;
            l = i + 2;
        } else {
            j = i + 1;
            i = u;
            u = j - 2;
        }
        lua_auxsort(a, j, i, lt);
    }
}
}  // namespace detail

template <class T, class Less>
void lua_sort(std::vector<T> &a, Less lt) {
    detail::lua_auxsort(a, 1, (int)a.size(), lt);
}

}  // namespace vl
