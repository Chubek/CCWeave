#include <TTyUtils/TTyUtils-HistoryFile.hpp>
#include <cassert>

int main() {
    const std::filesystem::path path{"/tmp/ttyutils-history-test"};
    std::filesystem::remove(path);
    ttyutils::hist::History history{path};
    assert(history.append({.text = "echo hi"}));
    assert(history.load());
    assert(history.match_prefix("echo").size() == 1);
    return 0;
}
