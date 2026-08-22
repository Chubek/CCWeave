#include <TTyUtils/TTyUtils-ListView.hpp>
#include <cassert>
struct Model : ttyutils::ui::ListModel { std::size_t size() const override{return 1;} std::string text(std::size_t) const override{return "row";} };
int main() { auto model=std::make_shared<Model>(); ttyutils::ui::ListView view{model}; view.select(0); assert(view.selected()==0); return 0; }
