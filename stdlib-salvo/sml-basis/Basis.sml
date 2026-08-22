(* SML Basis Salvo: portable source half of the required Basis surface.
 * Primitive-heavy structures are supplied by SMLBasis native/FFI bindings. *)
datatype 'a option = NONE | SOME of 'a
exception Option
exception Empty
exception Subscript

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
  fun filter p xs = rev (foldl (fn (x,a) => if p x then x::a else a) [] xs)
  fun null [] = true | null _ = false
  fun hd (x::_) = x | hd [] = raise Empty
 fun tl (_::xs) = xs | tl [] = raise Empty
  fun nth (x::_, 0) = x
    | nth (_::xs, n) = if n > 0 then nth (xs, n - 1) else raise Subscript
    | nth ([], _) = raise Subscript
  fun take (_, 0) = []
    | take (x::xs, n) = if n > 0 then x :: take (xs, n - 1) else raise Subscript
    | take ([], _) = raise Subscript
 fun drop (xs, 0) = xs
   | drop (_::xs, n) = if n > 0 then drop (xs, n - 1) else raise Subscript
   | drop ([], _) = raise Subscript
  fun length [] = 0
    | length (_::xs) = 1 + length xs
  fun exists p [] = false
    | exists p (x::xs) = p x orelse exists p xs
end

structure Array =
struct
  fun fromList xs = ref xs
  fun sub (a, i) =
    let fun get (x::_, 0) = x
          | get (_::xs, n) = get (xs, n - 1)
    in get (!a, i) end
  fun update (a, i, value) =
    let fun put ([], _) = []
          | put (_::xs, 0) = value::xs
          | put (x::xs, n) = x::put (xs, n - 1)
    in a := put (!a, i) end
end

(* Implementations may bind these names to the native companion. *)
structure SMLBasis =
struct
  val nativeLibrary = "sml_basis"
end
