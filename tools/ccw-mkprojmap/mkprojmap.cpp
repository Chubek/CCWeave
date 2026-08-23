// mk-projmap.cpp
//
// Generate an annotated project map using a local GGUF model via llama.cpp.
//
// Build (adjust paths to your llama.cpp checkout/build):
//   g++ -std=c++17 mk-projmap.cpp -o mk-projmap \
//       -I/path/to/llama.cpp/include \
//       -I/path/to/llama.cpp/ggml/include \
//       -L/path/to/llama.cpp/build/bin \
//       -lllama -lggml -lggml-base
//
// Run from the project root you want to map:
//   MKPROJMAP_MODEL=/path/to/model.gguf ./mk-projmap
//   # or drop a .gguf into ./.models and just: ./mk-projmap
//
// API note: this targets a recent llama.cpp (2025). If your headers are older,
// the likely renames are called out in comments near each call.

#include <llama.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <fnmatch.h> // POSIX glob matching, matches the Python fnmatch behavior

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
static constexpr size_t MAX_FILE_BYTES
    = 6000; // chars of file content fed to the model
static constexpr int N_CTX = 8192;
static constexpr int MAX_GEN_TOKENS = 80;

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------
static std::string
to_one_line (const std::string &in)
{
  std::string s;
  s.reserve (in.size ());
  bool prev_space = false;
  for (char c : in)
    {
      if (c == '\n' || c == '\r' || c == '\t')
        c = ' ';
      if (c == ' ')
        {
          if (prev_space)
            continue;
          prev_space = true;
        }
      else
        {
          prev_space = false;
        }
      s.push_back (c);
    }
  // trim
  size_t a = s.find_first_not_of (" ");
  size_t b = s.find_last_not_of (" ");
  if (a == std::string::npos)
    return "";
  return s.substr (a, b - a + 1);
}

// Read up to maxbytes from a file; flag empty and binary content.
static std::string
read_head (const fs::path &p, size_t maxbytes, bool &is_empty, bool &is_binary)
{
  is_empty = is_binary = false;
  std::ifstream f (p, std::ios::binary);
  if (!f)
    {
      is_empty = true;
      return {};
    }
  std::string data (maxbytes, '\0');
  f.read (data.data (), static_cast<std::streamsize> (maxbytes));
  data.resize (static_cast<size_t> (f.gcount ()));
  is_empty = data.empty ();
  is_binary = data.find ('\0') != std::string::npos;
  return data;
}

// ---------------------------------------------------------------------------
// Model discovery: MKPROJMAP_MODEL, else first *.gguf (sorted) in ./.models
// ---------------------------------------------------------------------------
static std::string
find_model ()
{
  if (const char *env = std::getenv ("MKPROJMAP_MODEL"); env && *env)
    return env;

  fs::path models = fs::current_path () / ".models";
  std::error_code ec;
  if (fs::is_directory (models, ec))
    {
      std::vector<fs::path> ggufs;
      for (const auto &e : fs::directory_iterator (models, ec))
        {
          if (e.is_regular_file () && e.path ().extension () == ".gguf")
            ggufs.push_back (e.path ());
        }
      std::sort (ggufs.begin (), ggufs.end ());
      if (!ggufs.empty ())
        return ggufs.front ().string ();
    }
  return {};
}

// ---------------------------------------------------------------------------
// .mapignore handling
// ---------------------------------------------------------------------------
struct IgnoreList
{
  std::vector<std::string> patterns;

  void
  load (const fs::path &file)
  {
    std::ifstream f (file);
    if (!f)
      return;
    std::string line;
    while (std::getline (f, line))
      {
        // strip comments/whitespace
        size_t hash = line.find ('#');
        if (hash != std::string::npos)
          line = line.substr (0, hash);
        size_t a = line.find_first_not_of (" \t\r\n");
        size_t b = line.find_last_not_of (" \t\r\n");
        if (a == std::string::npos)
          continue;
        patterns.push_back (line.substr (a, b - a + 1));
      }
  }

  // relpath uses '/' separators; name is the basename.
  bool
  matches (const std::string &relpath, const std::string &name) const
  {
    for (const auto &pat : patterns)
      {
        std::string p = pat;
        bool dir_only = false;
        if (!p.empty () && p.back () == '/')
          {
            dir_only = true;
            p.pop_back ();
          }
        (void)dir_only; // we match by name/path either way
        if (fnmatch (p.c_str (), name.c_str (), 0) == 0)
          return true;
        if (fnmatch (p.c_str (), relpath.c_str (), FNM_PATHNAME) == 0)
          return true;
        // pattern without slash should match any path segment
        if (p.find ('/') == std::string::npos
            && fnmatch (("*/" + p).c_str (), relpath.c_str (), 0) == 0)
          return true;
      }
    return false;
  }
};

// ---------------------------------------------------------------------------
// llama.cpp wrapper
// ---------------------------------------------------------------------------
class Llm
{
public:
  bool
  load (const std::string &path)
  {
    llama_backend_init ();

    llama_model_params mparams = llama_model_default_params ();
    mparams.load_mode = no_mmap ? LLAMA_LOAD_MODE_NONE
                            : LLAMA_LOAD_MODE_MMAP;

    if (const char *nm = std::getenv ("MKPROJMAP_NO_MMAP");
        nm && *nm && std::strcmp (nm, "0") != 0)
      mparams.use_mmap
          = false; // helps on network storage; see prior discussion

    // Older API: llama_load_model_from_file
    model_ = llama_model_load_from_file (path.c_str (), mparams);
    if (!model_)
      {
        std::fprintf (stderr, "failed to load model: %s\n", path.c_str ());
        return false;
      }

    // Older API: llama_model_get_vocab was llama_get_vocab / model->vocab
    vocab_ = llama_model_get_vocab (model_);

    llama_context_params cparams = llama_context_default_params ();
    cparams.n_ctx = N_CTX;
    cparams.n_batch = 2048;
    // Older API: llama_new_context_with_model
    ctx_ = llama_init_from_model (model_, cparams);
    if (!ctx_)
      {
        std::fprintf (stderr, "failed to create context\n");
        return false;
      }

    smpl_ = llama_sampler_chain_init (llama_sampler_chain_default_params ());
    llama_sampler_chain_add (smpl_, llama_sampler_init_top_k (40));
    llama_sampler_chain_add (smpl_, llama_sampler_init_top_p (0.95f, 1));
    llama_sampler_chain_add (
        smpl_,
        llama_sampler_init_temp (0.3f)); // low temp = steady descriptions
    llama_sampler_chain_add (smpl_,
                             llama_sampler_init_dist (LLAMA_DEFAULT_SEED));
    return true;
  }

  ~Llm ()
  {
    if (smpl_)
      llama_sampler_free (smpl_);
    if (ctx_)
      llama_free (ctx_);
    if (model_)
      llama_model_free (model_); // older API: llama_free_model
    llama_backend_free ();
  }

  std::string
  describe (const std::string &relpath, const std::string &content)
  {
    const std::string system = "You are a code analysis assistant. Given a "
                               "file's path and contents, "
                               "reply with exactly one concise sentence "
                               "describing what the file does. "
                               "No preamble, no markdown, just the sentence.";
    const std::string user = "File: " + relpath + "\n\nContents:\n" + content;

    std::string prompt = apply_template (system, user);
    clear_kv ();
    return to_one_line (generate (prompt, MAX_GEN_TOKENS));
  }

private:
  llama_model *model_ = nullptr;
  llama_context *ctx_ = nullptr;
  const llama_vocab *vocab_ = nullptr;
  llama_sampler *smpl_ = nullptr;

  void
  clear_kv ()
  {
    // Recent API. Older equivalents: llama_kv_self_clear(ctx_) or
    // llama_kv_cache_clear(ctx_).
    llama_memory_clear (llama_get_memory (ctx_), true);
  }

  std::string
  apply_template (const std::string &system, const std::string &user)
  {
    std::vector<llama_chat_message> msgs = {
      { "system", system.c_str () },
      { "user", user.c_str () },
    };
    const char *tmpl = llama_model_chat_template (model_, nullptr);
    std::vector<char> buf (1 << 15);
    int n = llama_chat_apply_template (tmpl, msgs.data (), msgs.size (),
                                       /*add_ass=*/true, buf.data (),
                                       (int)buf.size ());
    if (n > (int)buf.size ())
      {
        buf.resize (n);
        n = llama_chat_apply_template (tmpl, msgs.data (), msgs.size (), true,
                                       buf.data (), (int)buf.size ());
      }
    if (n < 0)
      return system + "\n\n" + user; // fallback if template missing
    return std::string (buf.data (), n);
  }

  std::vector<llama_token>
  tokenize (const std::string &text, bool add_special)
  {
    int n = -llama_tokenize (vocab_, text.c_str (), (int)text.size (), nullptr,
                             0, add_special, true);
    std::vector<llama_token> toks (n);
    llama_tokenize (vocab_, text.c_str (), (int)text.size (), toks.data (),
                    (int)toks.size (), add_special, true);
    return toks;
  }

  std::string
  piece (llama_token id)
  {
    char buf[256];
    int n = llama_token_to_piece (vocab_, id, buf, sizeof (buf), 0, true);
    if (n < 0)
      return {};
    return std::string (buf, n);
  }

  std::string
  generate (const std::string &prompt, int max_tokens)
  {
    std::vector<llama_token> toks = tokenize (prompt, /*add_special=*/true);
    llama_batch batch = llama_batch_get_one (toks.data (), (int)toks.size ());

    std::string out;
    for (int i = 0; i < max_tokens; ++i)
      {
        if (llama_decode (ctx_, batch))
          break;
        llama_token id = llama_sampler_sample (smpl_, ctx_, -1);
        if (llama_vocab_is_eog (vocab_, id))
          break;
        out += piece (id);
        batch = llama_batch_get_one (
            &id, 1); // note: points at local; reused each pass below
        // keep id alive across the next decode
        static thread_local llama_token last;
        last = id;
        batch = llama_batch_get_one (&last, 1);
      }
    return out;
  }
};

// ---------------------------------------------------------------------------
// Tree walk + map output
// ---------------------------------------------------------------------------
struct Walker
{
  Llm &llm;
  const IgnoreList &ignore;
  const fs::path &root;
  std::ostream &out;

  void
  run ()
  {
    out << root.filename ().string () << "/\n";
    walk (root, "");
  }

  void
  walk (const fs::path &dir, const std::string &prefix)
  {
    std::vector<fs::directory_entry> entries;
    std::error_code ec;
    for (const auto &e : fs::directory_iterator (dir, ec))
      {
        std::string name = e.path ().filename ().string ();
        std::string relpath
            = fs::relative (e.path (), root, ec).generic_string ();
        if (name == ".git" || name == ".models")
          continue;
        if (ignore.matches (relpath, name))
          continue;
        entries.push_back (e);
      }

    std::sort (entries.begin (), entries.end (),
               [] (const auto &a, const auto &b)
                 {
                   bool da = a.is_directory (), db = b.is_directory ();
                   if (da != db)
                     return da > db; // directories first
                   return a.path ().filename ().string ()
                          < b.path ().filename ().string ();
                 });

    for (size_t i = 0; i < entries.size (); ++i)
      {
        const auto &e = entries[i];
        bool last = (i + 1 == entries.size ());
        std::string name = e.path ().filename ().string ();
        std::string connector
            = last ? "\u2514\u2500\u2500 " : "\u251c\u2500\u2500 ";

        if (e.is_directory ())
          {
            out << prefix << connector << name << "/\n";
            walk (e.path (), prefix + (last ? "    " : "\u2502   "));
          }
        else
          {
            std::string relpath
                = fs::relative (e.path (), root).generic_string ();
            std::string desc = describe (e.path (), relpath);
            out << prefix << connector << name << "  # " << desc << "\n";
          }
      }
  }

  std::string
  describe (const fs::path &p, const std::string &relpath)
  {
    bool empty = false, binary = false;
    std::string content = read_head (p, MAX_FILE_BYTES, empty, binary);
    if (empty)
      return "(empty file)";
    if (binary)
      return "(binary file)";
    std::fprintf (stderr, "  describing %s ...\n", relpath.c_str ());
    return llm.describe (relpath, content);
  }
};

// ---------------------------------------------------------------------------
int
main ()
{
  std::string model_path = find_model ();
  if (model_path.empty ())
    {
      std::fprintf (stderr, "no model found: set MKPROJMAP_MODEL or place a "
                            ".gguf in ./.models\n");
      return 1;
    }
  std::fprintf (stderr, "loading model: %s\n", model_path.c_str ());

  Llm llm;
  if (!llm.load (model_path))
    return 1;

  fs::path root = fs::current_path ();
  IgnoreList ignore;
  ignore.load (root / ".mapignore");

  std::string out_name = root.filename ().string () + ".map";
  std::ofstream out (out_name);
  if (!out)
    {
      std::fprintf (stderr, "cannot write %s\n", out_name.c_str ());
      return 1;
    }

  Walker walker{ llm, ignore, root, out };
  walker.run ();

  std::fprintf (stderr, "wrote %s\n", out_name.c_str ());
  return 0;
}
