#include <TTyTk/TTyTk-Automaton.hpp>

#include <cassert>
#include <string>

int main() {
    ttytk::Automaton parser;
    std::string printed;
    std::string csi;
    std::string osc;
    parser.on_print = [&](const std::string_view text) { printed += text; };
    parser.on_csi = [&](const std::string_view value) { csi = value; };
    parser.on_osc = [&](const std::string_view value) { osc = value; };
    parser.feed("ok\x1B[31m\x1B]2;title\x07");
    assert(printed == "ok");
    assert(csi == "31m");
    assert(osc == "2;title");
}
