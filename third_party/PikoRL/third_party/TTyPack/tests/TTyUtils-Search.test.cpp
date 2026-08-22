#include <TTyUtils/TTyUtils-Search.hpp>
#include <cassert>

int main() {
    ttyutils::textbuf::Buffer buffer{"TODO todo"};
    auto query = ttyutils::search::Query::literal("todo", false);
    assert(ttyutils::search::find_all(buffer.snapshot(), query).size() == 2);
    ttyutils::search::Replacer replacer{buffer, query, "FIX"};
    assert(replacer.replace_all() == 2);
    assert(buffer.text() == "FIX FIX");
    return 0;
}
