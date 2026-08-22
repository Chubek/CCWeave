#include <TTyUtils/TTyUtils-SyntaxGrammar.hpp>
#include <cassert>
#include <fstream>

int main() {
    const std::filesystem::path path{"/tmp/ttyutils-test.tmLanguage.json"};
    std::ofstream{path} << R"({"scopeName":"source.cpp","name":"C++","fileTypes":["cpp"],"keywords":["int"]})";
    auto grammar = ttyutils::syntax::load_tm(path);
    assert(grammar);
    assert(grammar.value().matches_filename("main.cpp"));
    return 0;
}
