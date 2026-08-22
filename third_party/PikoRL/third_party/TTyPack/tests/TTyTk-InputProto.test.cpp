#include <TTyTk/TTyTk-InputProto.hpp>

#include <cassert>

int main() {
    const auto key = ttytk::input::decode_seq("\x1B[97;5u");
    assert(key);
    const auto* decoded_key = std::get_if<ttytk::input::Key>(&key.value());
    assert(decoded_key != nullptr);
    assert(decoded_key->codepoint == U'a');
    assert(decoded_key->modifiers == 4);

    const auto mouse = ttytk::input::decode_seq("\x1B[<0;12;4M");
    assert(mouse);
    const auto* decoded_mouse = std::get_if<ttytk::input::Mouse>(&mouse.value());
    assert(decoded_mouse != nullptr);
    assert(decoded_mouse->pressed && decoded_mouse->x == 12 && decoded_mouse->y == 4);

    const auto paste = ttytk::input::decode_seq("\x1B[200~hello\x1B[201~");
    assert(paste);
    assert(std::get<ttytk::input::Paste>(paste.value()).text == "hello");
}
