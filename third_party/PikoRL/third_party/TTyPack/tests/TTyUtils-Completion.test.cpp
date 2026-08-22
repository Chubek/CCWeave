#include <TTyUtils/TTyUtils-Completion.hpp>
#include <cassert>
#include <memory>

int main() {
    ttyutils::completion::Engine engine;
    engine.add(std::make_unique<ttyutils::completion::WordSource>(
        std::vector<std::string>{"alpha", "beta"}));
    const auto result = engine.query("al");
    assert(result.size() == 1);
    assert(result.front().text == "alpha");
    return 0;
}
