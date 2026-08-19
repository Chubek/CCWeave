(* SML Basis Salvo: portable source half of the required Basis surface.
 * Primitive-heavy structures are supplied by SMLBasis native/FFI bindings. *)
exception Option
exception Empty

structure Option =
struct
  fun getOpt (SOME x, _) = x | getOpt (NONE, d) = d
  fun isSome (SOME _) = true | isSome NONE = false
  fun valOf (SOME x) = x | valOf NONE = raise Option
end

structure List =
struct
  fun rev xs = let fun loop ([], a) = a | loop (x::r, a) = loop (r, x::a)
               in loop (xs, []) end
  fun map f xs = rev (List.foldl (fn (x,a) => f x :: a) [] xs)
  fun foldl f z [] = z | foldl f z (x::xs) = foldl f (f (x,z)) xs
  fun null [] = true | null _ = false
  fun hd (x::_) = x | hd [] = raise Empty
  fun tl (_::xs) = xs | tl [] = raise Empty
end

(* Implementations may bind these names to the native companion. *)
structure SMLBasis =
struct
  val nativeLibrary = "sml_basis"
end
