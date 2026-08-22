#include <TTyUtils/TTyUtils-MarkdownView.hpp>
#include <cassert>
int main() { ttyutils::mdview::MarkdownView view; view.set_text("# Title\ntext"); assert(view.toc().size()==1); assert(ttyutils::mdview::render("# x").find("x")!=std::string::npos); return 0; }
