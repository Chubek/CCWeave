#include <TTyUtils/TTyUtils-App.hpp>
#include <cassert>

int main() {
    ttyutils::app::App application;
    application.quit(7);
    assert(application.run() == 7);
    return 0;
}
