#include <TTyUtils/TTyUtils-Serial.hpp>
#include <cassert>
int main() { const auto devices=ttyutils::serial::enumerate(); (void)devices; assert(ttyutils::serial::Settings{}.baud==115200); return 0; }
