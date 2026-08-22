#include <TTyUtils/TTyUtils-ScrollBar.hpp>
#include <cassert>
struct Scroll : ttyutils::ui::Scrollable { std::size_t at=0; std::size_t offset() const override{return at;} void scroll_to(std::size_t v) override{at=v;} std::size_t length() const override{return 10;} };
int main() { Scroll model; ttyutils::ui::ScrollBar bar; bar.attach(model); bar.scroll_by(3); assert(model.at==3); return 0; }
