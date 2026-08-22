#include <TTyUtils/TTyUtils-SyntaxHighlight.hpp>
#include <cassert>

int main() {
    ttyutils::syntax::Grammar grammar;
    grammar.keywords = {"int"};
    grammar.root_scope = ttyutils::syntax::Scope{"keyword"};
    ttyutils::textbuf::Buffer buffer{"int x;"};
    ttyutils::hl::Highlighter highlighter{grammar};
    highlighter.attach(buffer);
    assert(highlighter.line_spans(0).items().size() == 1);
    return 0;
}
