#include <TTyTk/TTyTk-PuppetTools.hpp>
#include <cassert>
int main() { ttytk::Puppet puppet; puppet.observe("ready"); assert(puppet.wait_for_text("ready")); }
