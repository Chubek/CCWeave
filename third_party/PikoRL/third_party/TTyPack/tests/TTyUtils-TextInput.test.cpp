#include <TTyUtils/TTyUtils-TextInput.hpp>
#include <cassert>
int main() { ttyutils::ui::TextInput input; input.set_text("abc"); assert(input.valid()); ttyutils::ui::PasswordInput password; password.set_text("secret"); assert(password.display_text()=="******"); return 0; }
