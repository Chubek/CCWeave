#include <TTyUtils/TTyUtils-HexView.hpp>
#include <cassert>
int main() { ttyutils::hexview::HexView view; view.set_data({1,2,3}); assert(view.data().size()==3); assert(ttyutils::hexview::Inspector::u16(view.data(),0)==513); return 0; }
