#include <TTyUtils/TTyUtils-ImageView.hpp>
#include <cassert>
int main() { ttyutils::imgview::ImageView view; view.set_fit(ttyutils::imgview::FitMode::Cover); assert(view.fit()==ttyutils::imgview::FitMode::Cover); return 0; }
