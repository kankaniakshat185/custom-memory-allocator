// Demo: an ordinary C++ program whose every new/delete, and so every standard
// container, runs on myalloc. Nothing in the code mentions the allocator
// except the stats at the end: linking the myalloc_new library is the only
// change (ADR-015).
//
//   cmake --build build --target stl_demo && ./build/stl_demo
//   MYALLOC_LEAK_REPORT=1 ./build/stl_demo leak   # leaks one block on purpose
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "myalloc/myalloc.h"

// External linkage, so the compiler must perform the store and cannot elide
// the new (it may drop allocations whose result is never observable).
std::string* g_leaked = nullptr;

namespace {

void print_stats(const char* when) {
    my_alloc_stats_t s;
    my_alloc_stats(&s);
    std::printf("%-26s live blocks %7zu  in use %9zu B  peak %9zu B  free blocks %5zu\n", when,
                s.live_blocks, s.bytes_in_use, s.peak_bytes_in_use, s.free_blocks);
}

}  // namespace

int main(int argc, char** argv) {
    print_stats("start");
    {
        // Word frequencies over a generated text.
        std::ostringstream text;
        for (int i = 0; i < 50000; ++i) text << "token" << (i * 7919) % 3001 << ' ';
        std::unordered_map<std::string, int> counts;
        std::istringstream in(text.str());
        for (std::string w; in >> w;) ++counts[w];

        // Top ten by count, then by word.
        std::vector<std::pair<std::string, int>> top(counts.begin(), counts.end());
        std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        top.resize(10);

        std::map<int, std::vector<std::string>> by_count;
        for (const auto& [w, c] : counts) by_count[c].push_back(w);
        auto owner = std::make_shared<std::string>("shared payload");

        print_stats("containers built");
        std::printf("distinct words %zu, distinct counts %zu, top word %s x%d\n", counts.size(),
                    by_count.size(), top.front().first.c_str(), top.front().second);
    }
    print_stats("containers destroyed");

    if (argc > 1 && std::strcmp(argv[1], "leak") == 0) {
        g_leaked = new std::string(100, 'x');  // never deleted: shows up in the leak report
        print_stats("after deliberate leak");
    }
    return 0;
}
