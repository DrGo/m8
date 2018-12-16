#include "m8/m8.hh"

#include "m8/ast.hh"
#include "m8/reader.hh"
#include "m8/writer.hh"

#include "ob/sys_command.hh"
#include "ob/string.hh"
#include "ob/http.hh"

#include "ob/term.hh"
namespace aec = OB::Term::ANSI_Escape_Codes;

#include "lib/json.hh"
using Json = nlohmann::json;

#include <ctime>
#include <cctype>
#include <cstddef>

#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <regex>
#include <functional>
#include <stdexcept>
#include <future>
#include <iterator>
#include <stack>
#include <algorithm>
#include <deque>
#include <optional>

M8::M8()
{
  core_macros();
}

M8::~M8()
{
}

void M8::core_macros()
{
  set_core("m8:include_once",
    "include a files contents once",
    "{str}",
    "{b}{!str_s}{e}",
    [&](auto& ctx) {
    auto str = ctx.args.at(1);
    if (str.empty())
    {
      return -1;
    }
    try
    {
      auto const& name = ctx.args.at(1);

      if (includes_.find(name) != includes_.end())
      {
        return 0;
      }
      includes_.emplace(name);

      ctx.core->w.write(ctx.core->buf);
      ctx.core->w.flush();
      ctx.core->buf.clear();
      parse(name, ctx.core->ofile);
    }
    catch (std::exception const& e)
    {
      ctx.err_msg = "an error occurred while parsing the included file";
      return -1;
    }
    return 0;
    });

  set_core("m8:include",
    "include a files contents",
    "{str}",
    "{b}{!str_s}{e}",
    [&](auto& ctx) {
    auto str = ctx.args.at(1);
    if (str.empty())
    {
      return -1;
    }
    try
    {
      ctx.core->w.write(ctx.core->buf);
      ctx.core->w.flush();
      ctx.core->buf.clear();
      parse(ctx.args.at(1), ctx.core->ofile);
    }
    catch (std::exception const& e)
    {
      ctx.err_msg = "an error occurred while parsing the included file";
      return -1;
    }
    return 0;
    });

  set_core("m8:file",
    "current file name",
    "{empty}",
    "{empty}",
    [&](auto& ctx) {
    ctx.str = ctx.core->ifile;
    return 0;
    });

  set_core("m8:line",
    "current line number",
    "{empty}",
    "{empty}",
    [&](auto& ctx) {
    ctx.str = std::to_string(ctx.core->r.row());
    return 0;
    });

  set_core("m8:ns+",
    "namespace block",
    "{empty}",
    "{empty}",
    [&](auto& ctx) {
    macros_.add_scope();
    return 0;
    });

  set_core("m8:ns-",
    "namespace block",
    "{empty}",
    "{empty}",
    [&](auto& ctx) {
    macros_.rm_scope();
    return 0;
    });

  auto const fn_test_hook_info = [&](auto& ctx) {
    std::stringstream st;
    st << ctx.args.at(1);
    char t;
    st >> t;

    std::stringstream ss;

    auto const stringify_hooks = [&](auto t) {
      if (auto h = get_hooks(t))
      {
        for (auto const& e : *h)
        {
          ss << "k: " << e.key << "\nv: " << e.val << "\n";
        }
      }
    };

    if (t == 'b')
    {
      stringify_hooks(M8::Htype::begin);
    }
    else if (t == 'm')
    {
      stringify_hooks(M8::Htype::macro);
    }
    else if (t == 'r')
    {
      stringify_hooks(M8::Htype::res);
    }
    else if (t == 'e')
    {
      stringify_hooks(M8::Htype::end);
    }
    else
    {
      return -1;
    }

    ctx.str = ss.str();
    return 0;
  };
  auto const fn_test_hook_rm = [&](auto& ctx) {
    std::stringstream ss;
    ss << ctx.args.at(1);
    char t;
    ss >> t;
    auto s1 = ctx.args.at(2);

    if (t == 'b')
    {
      rm_hook(M8::Htype::begin, s1);
    }
    else if (t == 'm')
    {
      rm_hook(M8::Htype::macro, s1);
    }
    else if (t == 'r')
    {
      rm_hook(M8::Htype::res, s1);
    }
    else if (t == 'e')
    {
      rm_hook(M8::Htype::end, s1);
    }
    else
    {
      return -1;
    }
    return 0;
  };
  auto const fn_test_hook_add = [&](auto& ctx) {
    std::stringstream ss;
    ss << ctx.args.at(1);
    char t;
    ss >> t;
    auto s1 = ctx.args.at(2);
    auto s2 = ctx.args.at(3);

    if (t == 'b')
    {
      set_hook(M8::Htype::begin, {s1, s2});
    }
    else if (t == 'm')
    {
      set_hook(M8::Htype::macro, {s1, s2});
    }
    else if (t == 'r')
    {
      set_hook(M8::Htype::res, {s1, s2});
    }
    else if (t == 'e')
    {
      set_hook(M8::Htype::end, {s1, s2});
    }
    else
    {
      return -1;
    }
    return 0;
  };
  set_macro("m8:hook+",
    "test add hook macro",
    "1[bre] str str",
    {
      {"{b}([bmre]{1}){ws}{!str_s}{ws}{!str_s}{e}", fn_test_hook_add},
    });
  set_macro("m8:hook-",
    "test remove hook macro",
    "1[bre] str",
    {
      {"{b}([bmre]{1}){ws}{!str_s}{e}", fn_test_hook_rm},
    });
  set_macro("m8:hook:info",
    "test info hook macro",
    "1[bre]",
    {
      {"{b}([bmre]{1}){e}", fn_test_hook_info},
    });

}

std::string M8::error(Tmacro const& t, std::string const& ifile, std::string const& title, std::string const& body)
{
  std::stringstream ss;

  ss
  << aec::wrap("Error: ", aec::fg_red)
  << title
  << " '" << aec::wrap(t.name, aec::fg_white) << "' "
  << ifile << ":" << t.line_start;
  if (t.line_start != t.line_end)
  {
    ss << "-" << t.line_end;
  }
  ss
  << ":" << (t.begin + delim_start_.size() + 1)
  << "\n";

  ss
  << aec::wrap(delim_start_, aec::fg_cyan) << " "
  << aec::wrap(t.name, aec::fg_white) << " ";
  if (t.match.empty() && ! t.args.empty())
  {
    ss << aec::wrap(t.args, aec::fg_red) << " ";
  }
  else
  {
    for (std::size_t i = 1; i < t.match.size(); ++i)
    {
      if (i % 2 == 0)
      {
        ss << aec::wrap(t.match.at(i), aec::fg_yellow) << " ";
      }
      else
      {
        ss << aec::wrap(t.match.at(i), aec::fg_blue) << " ";
      }
    }
  }
  ss
  << aec::wrap(delim_end_, aec::fg_cyan)
  << "\n";

  // ss
  // << std::string(delim_start_.size() + 1, ' ')
  // << aec::wrap(std::string(t.name.size(), '^'), aec::fg_red)
  // << "\n";

  if (! body.empty())
  {
    ss
    << body
    << "\n";
  }

  return ss.str();
}

void M8::set_debug(bool val)
{
  settings_.debug = val;
}

void M8::set_comment(std::string str)
{
  comment_ = str;
}

void M8::set_ignore(std::string str)
{
  ignore_ = str;
}

void M8::set_copy(bool val)
{
  settings_.copy = val;
}

void M8::set_readline(bool val)
{
  settings_.readline = val;
}

void M8::set_core(std::string const& name, std::string const& info,
  std::string const& usage, std::string regex, macro_fn func)
{
  regex = OB::String::format(regex, rx_grammar_);
  // macros_[name] = {Mtype::core, name, info, usage, {{regex, func}}, {}};
  macros_.insert_or_assign(name, Macro({Mtype::core, name, info, usage, {{regex, func}}, {}}));
}

void M8::set_macro(std::string const& name, std::string const& info,
  std::string const& usage, std::string regex)
{
  regex = OB::String::format(regex, rx_grammar_);
  // macros_[name] = {Mtype::external, name, info, usage, {{regex, nullptr}}, {}};
  macros_.insert_or_assign(name, Macro({Mtype::external, name, info, usage, {{regex, nullptr}}, {}}));
}

void M8::set_macro(std::string const& name, std::string const& info,
  std::string const& usage, std::string regex, std::string const& url)
{
  regex = OB::String::format(regex, rx_grammar_);
  // macros_[name] = {Mtype::remote, name, info, usage, {{regex, nullptr}}, url};
  macros_.insert_or_assign(name, Macro({Mtype::remote, name, info, usage, {{regex, nullptr}}, url}));
}

void M8::set_macro(std::string const& name, std::string const& info,
  std::string const& usage, std::string regex, macro_fn func)
{
  regex = OB::String::format(regex, rx_grammar_);
  // macros_[name] = {Mtype::internal, name, info, usage, {{regex, func}}, {}};
  macros_.insert_or_assign(name, Macro({Mtype::internal, name, info, usage, {{regex, func}}, {}}));
}

void M8::set_macro(std::string const& name, std::string const& info,
  std::string const& usage,
  std::vector<std::pair<std::string, macro_fn>> rx_fn)
{
  for (auto& e : rx_fn)
  {
    e.first = OB::String::format(e.first, rx_grammar_);
  }
  // macros_[name] = {Mtype::internal, name, info, usage, rx_fn, {}};
  macros_.insert_or_assign(name, Macro({Mtype::internal, name, info, usage, rx_fn, {}}));
}

void M8::set_delimits(std::string const& delim_start, std::string const& delim_end)
{
  if (delim_start == delim_end)
  {
    throw std::runtime_error("start and end delimeter can't be the same value");
  }
  delim_start_ = delim_start;
  delim_end_ = delim_end;
  rx_grammar_["DS"] = delim_start_;
  rx_grammar_["DE"] = delim_end_;
}

std::string M8::list_macros() const
{
  std::stringstream ss;

  ss << "M8:\n\n";

  for (auto const& e : macros_)
  {
    ss
    << e.second.name << "\n"
    << "  " << e.second.usage << "\n"
    << "  " << e.second.info << "\n";
    for (auto const& e : e.second.rx_fn)
    {
      ss << "  " << e.first << "\n";
    }
  }

  return ss.str();
}

std::string M8::macro_info(std::string const& name) const
{
  std::stringstream ss;
  if (macros_.find(name) == macros_.end())
  {
    ss << aec::wrap("Error: ", aec::fg_red);
    ss << "Undefined name '" << aec::wrap(name, aec::fg_white) << "'\n";

    // lookup similar macro suggestion
    ss << "  Looking for similar names...\n";
    auto similar_names = suggest_macro(name);
    if (similar_names.size() > 0)
    {
      ss << "  Did you mean: " << aec::fg_green;
      for (auto const& e : similar_names)
      {
        ss << e << " ";
      }
      ss << aec::reset << "\n";
    }
    else
    {
      ss << aec::fg_magenta << "  No suggestions found.\n" << aec::reset;
    }
  }
  else
  {
    auto const& e = macros_.at(name);
    ss
    << e.name << "\n"
    << "  " << e.info << "\n"
    << "  " << e.usage << "\n";
    for (auto const& e : e.rx_fn)
    {
      ss << "  " << e.first << "\n";
    }
  }
  return ss.str();
}

void M8::set_config(std::string file_name)
{
  // add external macros
  if (file_name.empty())
  {
    // default config file location -> ~/.m8.json
    file_name = env_var("HOME") + "/.m8.json";
  }

  // open the config file
  std::ifstream file {file_name};
  if (! file.is_open())
  {
    if (settings_.debug)
    {
      std::cerr << "Debug: could not open config file\n";
    }
    return;
  }

  // read in the config file into memory
  file.seekg(0, std::ios::end);
  std::size_t size (static_cast<std::size_t>(file.tellg()));
  std::string content (size, ' ');
  file.seekg(0);
  file.read(&content[0], static_cast<std::streamsize>(size));

  // parse the contents
  Json j = Json::parse(content);
  for (auto const& e : j["macros"])
  {
    // add remote macro
    if (e.count("url") == 1)
    {
      set_macro(
        e["name"].get<std::string>(),
        e["info"].get<std::string>(),
        e["usage"].get<std::string>(),
        e["regex"].get<std::string>(),
        e["url"].get<std::string>());
    }

    // add external macro
    else
    {
      set_macro(
        e["name"].get<std::string>(),
        e["info"].get<std::string>(),
        e["usage"].get<std::string>(),
        e["regex"].get<std::string>());
    }
  }
}

std::string M8::summary() const
{
  std::stringstream ss; ss
  << aec::wrap("Summary\n", aec::fg_magenta)
  << aec::wrap("  Total      ", aec::fg_magenta) << aec::wrap(stats_.macro, aec::fg_green) << "\n"
  << aec::wrap("    Internal ", aec::fg_magenta) << aec::wrap(stats_.internal, aec::fg_green) << "\n"
  << aec::wrap("    External ", aec::fg_magenta) << aec::wrap(stats_.external, aec::fg_green) << "\n"
  << aec::wrap("    Remote   ", aec::fg_magenta) << aec::wrap(stats_.remote, aec::fg_green) << "\n"
  << aec::wrap("  passes     ", aec::fg_magenta) << aec::wrap(stats_.pass, aec::fg_green) << "\n"
  << aec::wrap("  warnings   ", aec::fg_magenta) << aec::wrap(stats_.warning, aec::fg_green) << "\n";
  return ss.str();
}

std::vector<std::string> M8::suggest_macro(std::string const& name) const
{
  std::vector<std::string> similar_names;

  std::smatch similar_match;
  int len = (name.size() / 1.2);
  // if (len < 2) len = name.size();
  std::stringstream escaped_name;
  for (auto const& e : name)
  {
    if (std::isalnum(e))
    {
      escaped_name << e;
    }
    else
    {
      escaped_name << "\\" << e;
    }
  }
  std::string similar_regex {"^.*[" + escaped_name.str() + "]{" + std::to_string(len) + "}.*$"};

  for (auto const& e : macros_)
  {
    if (std::regex_match(e.first, similar_match, std::regex(similar_regex, std::regex::icase)))
    {
      similar_names.emplace_back(similar_match[0]);
    }
  }

  std::sort(similar_names.begin(), similar_names.end(),
  [](std::string const& lhs, std::string const& rhs) {
    return lhs.size() < rhs.size();
  });

  if (similar_names.size() > 8)
  {
    similar_names.erase(similar_names.begin() + 8, similar_names.end());
  }

  return similar_names;
}

void M8::set_hook(Htype t, Hook h)
{
  auto const insert_hook = [&](auto& hooks) {
    for (auto& e : hooks)
    {
      if (e.key == h.key)
      {
        e.val = h.val;
        return;
      }
    }
    hooks.emplace_back(h);
  };

  switch (t)
  {
    case Htype::begin:
      insert_hook(h_begin_);
      return;
    case Htype::macro:
      insert_hook(h_macro_);
      return;
    case Htype::res:
      insert_hook(h_res_);
      return;
    case Htype::end:
      insert_hook(h_end_);
      return;
    default:
      return;
  }
}

std::optional<M8::Hooks> M8::get_hooks(Htype t) const
{
  switch (t)
  {
    case Htype::begin: return h_begin_;
    case Htype::macro: return h_macro_;
    case Htype::res: return h_res_;
    case Htype::end: return h_end_;
    default: return {};
  }
}

void M8::rm_hook(Htype t, std::string key)
{
  auto const rm_key = [&](auto& m) {
    long int i {0};
    for (auto& e : m)
    {
      if (e.key == key)
      {
        m.erase(m.begin() + i);
      }
      ++i;
    }
  };

  switch (t)
  {
    case Htype::begin:
      rm_key(h_begin_);
      break;
    case Htype::macro:
      rm_key(h_macro_);
      break;
    case Htype::res:
      rm_key(h_res_);
      break;
    case Htype::end:
      rm_key(h_end_);
      break;
    default:
      break;
  }
}

void M8::run_hooks(Hooks const& h, std::string& s)
{
  for (auto const& e : h)
  {
    std::string str = s;
    s.clear();
    std::smatch match;

    while (std::regex_search(str, match, std::regex(e.key)))
    {
      std::unordered_map<std::string, std::string> m;
      for (std::size_t i = 1; i < match.size(); ++i)
      {
        m[std::to_string(i)] = match[i];
        m["DS"] = delim_start_;
        m["DE"] = delim_end_;
      }
      std::string ns {e.val};
      ns = OB::String::format(ns, m);
      s += std::string(match.prefix()) + ns;
      if (str == match.suffix())
      {
        str.clear();
        break;
      }
      str = match.suffix();
      if (str.empty()) break;
    }
    s += str;
  }
}

void M8::parse(std::string const& _ifile, std::string const& _ofile)
{
  // init the reader
  Reader r;
  if (! settings_.readline || ! _ifile.empty())
  {
    r.open(_ifile);
  }

  // init the writer
  Writer w;
  if (! _ofile.empty())
  {
    w.open(_ofile);
  }

  auto& ast = ast_.ast;
  std::stack<Tmacro> stk;
  // Cache cache_ {_ifile};

  std::string buf;
  std::string line;

  while(r.next(line))
  {
    buf.clear();

    // check for empty line
    if (line.empty())
    {
      // TODO add flag to ignore empty lines
      if (stk.empty())
      {
        // if (! _ofile.empty() && settings_.copy)
        if (settings_.copy)
        {
          w.write("\n");
        }
        continue;
      }
      else
      {
        auto& t = stk.top();
        t.str += "\n";
        continue;
      }
    }

    // commented out line
    if (! comment_.empty())
    {
      auto pos = line.find_first_not_of(" \t");
      if (pos != std::string::npos)
      {
        if (line.compare(pos, comment_.size(), comment_) == 0)
        {
          continue;
        }
      }
    }

    // whitespace indentation
    std::size_t indent {0};
    char indent_char {' '};
    {
      std::string e {line.at(0)};
      if (e.find_first_of(" \t") != std::string::npos)
      {
        std::size_t count {0};
        for (std::size_t i = 0; i < line.size(); ++i)
        {
          e = line.at(i);
          if (e.find_first_not_of(" \t") != std::string::npos)
          {
            break;
          }
          ++count;
        }
        indent = count;
        indent_char = line.at(0);
      }
    }

    // find and replace macro words
    run_hooks(h_begin_, line);

    // parse line char by char for either start or end delim
    for (std::size_t i = 0; i < line.size(); ++i)
    {
      // case start delimiter
      if (line.at(i) == delim_start_.at(0))
      {
        std::size_t pos_start = line.find(delim_start_, i);
        if (pos_start != std::string::npos && pos_start == i)
        {
          if (i > 0 && line.at(i - 1) == '`')
          {
            goto regular_char;
          }

          // stack operations
          auto t = Tmacro();
          t.line_start = r.row();
          t.begin = pos_start;
          // if (! stk.empty())
          // {
          //   append placeholder
          //   stk.top().str += "[%" + std::to_string(stk.top().children.size()) + "]";
          // }

          stk.push(t);

          i += delim_start_.size() - 1;
          continue;
        }
      }

      // case end delimiter
      if (line.at(i) == delim_end_.at(0))
      {
        std::size_t pos_end = line.find(delim_end_, i);
        if (pos_end != std::string::npos && pos_end == i)
        {
          if (i + delim_end_.size() < line.size() && line.at(i + delim_end_.size()) == '`')
          {
            goto regular_char;
          }

          // stack operations
          if (stk.empty())
          {
            auto t = Tmacro();
            t.name = delim_start_;
            t.line_start = r.row();
            t.line_end = r.row();
            t.begin = i;
            std::cerr << error(t, _ifile, "missing opening delimiter", r.line());
            if (settings_.readline)
            {
              stk = std::stack<Tmacro>();
              break;
            }
            throw std::runtime_error("missing opening delimiter");
          }
          else
          {
            auto t = stk.top();
            stk.pop();

            t.line_end = r.row();
            t.end = pos_end;

            // parse str into name and args
            {
              std::smatch match;
              std::string name_args {"^\\s*([^\\s]+)\\s*([^\\r]*?)\\s*$"};
              // std::string name_args {"^\\s*([^\\s]+)\\s*(?:M8!|)([^\\r]*?)(?:!8M|$)$"};
              if (std::regex_match(t.str, match, std::regex(name_args)))
              {
                t.name = match[1];
                t.args = match[2];

                // find and replace macro words
                run_hooks(h_macro_, t.name);
                run_hooks(h_macro_, t.args);
              }
              else
              {
                std::cerr << error(t, _ifile, "invalid format", "could not parse macro name or arguments");
                if (settings_.readline)
                {
                  stk = std::stack<Tmacro>();
                  break;
                }
                throw std::runtime_error("invalid format");
              }
            }

            // validate name and args
            {
              auto const it = macros_.find(t.name);
              if (it == macros_.end())
              {
                // lookup similar macro suggestion
                std::stringstream ss; ss
                << "looking for similar names..."
                << "\n";
                auto similar_names = suggest_macro(t.name);
                if (similar_names.size() > 0)
                {
                  ss
                  << "did you mean: "
                  << aec::fg_green;
                  for (auto const& e : similar_names)
                  {
                    ss << e << " ";
                  }
                  ss << aec::reset;
                }
                else
                {
                  ss << aec::wrap("no suggestions found.", aec::fg_red);
                }

                std::cerr << error(t, _ifile, "undefined name", ss.str());

                if (settings_.readline)
                {
                  stk = std::stack<Tmacro>();
                  buf.clear();
                  break;
                }
                throw std::runtime_error("undefined name");
              }

              if (it->second.rx_fn.at(0).first.empty())
              {
                std::vector<std::string> reg_num {
                  {"^[\\-+]{0,1}[0-9]+$"},
                  {"^[\\-+]{0,1}[0-9]*\\.[0-9]+$"},
                  {"^[\\-+]{0,1}[0-9]+e[\\-+]{0,1}[0-9]+$"},
                  // {"^[\\-|+]{0,1}[0-9]+/[\\-|+]{0,1}[0-9]+$"},
                };

                std::vector<std::string> reg_str {
                  {"^([^`\\\\]*(?:\\\\.[^`\\\\]*)*)$"},
                  {"^([^'\\\\]*(?:\\\\.[^'\\\\]*)*)$"},
                  {"^([^\"\\\\]*(?:\\\\.[^\"\\\\]*)*)$"},
                };

                // complete arg string as first parameter
                t.match.emplace_back(t.args);
                std::vector<bool> valid_args;

                for (std::size_t j = 0; j < t.args.size(); ++j)
                {
                  std::stringstream ss; ss << t.args.at(j);
                  auto s = ss.str();
                  // std::cerr << "arg:" << s << "\n";
                  if (s.find_first_of(" \n\t") != std::string::npos)
                  {
                    continue;
                  }
                  // if (s.find_first_of(".") != std::string::npos)
                  // {
                  //   // macro
                  //   t.match.emplace_back(std::string());
                  //   for (;j < t.args.size() && t.args.at(j) != '.'; ++j)
                  //   {
                  //     t.match.back() += t.args.at(j);
                  //   }
                  //   std::cerr << "Arg-Macro:\n~" << t.match.back() << "~\n";
                  //   for (auto const& e : reg_num)
                  //   {
                  //     std::smatch m;
                  //     if (std::regex_match(t.match.back(), m, std::regex(e)))
                  //     {
                  //       // std::cerr << "ArgValid\nmacro\n" << t.match.back() << "\n\n";
                  //     }
                  //   }
                  // }
                  if (s.find_first_of(".-+0123456789") != std::string::npos)
                  {
                    // num
                    // std::cerr << "Num\n";
                    t.match.emplace_back(std::string());
                    for (;j < t.args.size() && t.args.at(j) != ' '; ++j)
                    {
                      t.match.back() += t.args.at(j);
                    }
                    // std::cerr << "Arg-Num\n" << t.match.back() << "\n";
                    bool invalid {true};
                    for (auto const& e : reg_num)
                    {
                      std::smatch m;
                      if (std::regex_match(t.match.back(), m, std::regex(e)))
                      {
                        invalid = false;
                        // std::cerr << "ArgValid\nnum\n" << t.match.back() << "\n\n";
                      }
                    }
                    if (invalid)
                    {
                      goto invalid_arg;
                    }
                    // if (t.match.back().find("/") != std::string::npos)
                    // {
                    //   auto n1 = t.match.back().substr(0, t.match.back().find("/"));
                    //   auto n2 = t.match.back().substr(t.match.back().find("/") + 1);
                    //   auto n = std::stod(n1) / std::stod(n2);
                    //   std::stringstream ss; ss << n;
                    //   t.match.back() = ss.str();
                    //   std::cerr << "Simplified:\n" << t.match.back() << "\n\n";
                    // }
                  }
                  else if (s.find_first_of("\"") != std::string::npos)
                  {
                    // str
                    // std::cerr << "Str\n";
                    t.match.emplace_back("");
                    ++j; // skip start quote
                    bool escaped {false};
                    for (;j < t.args.size(); ++j)
                    {
                      if (! escaped && t.args.at(j) == '\"')
                      {
                        // skip end quote
                        break;
                      }
                      if (t.args.at(j) == '\\')
                      {
                        escaped = true;
                        continue;
                      }
                      if (escaped)
                      {
                        t.match.back() += "\\";
                        t.match.back() += t.args.at(j);
                        escaped = false;
                        continue;
                      }
                      t.match.back() += t.args.at(j);
                    }
                    // std::cerr << "Arg-Str\n" << t.match.back() << "\n";
                    bool invalid {true};
                    for (auto const& e : reg_str)
                    {
                      std::smatch m;
                      if (std::regex_match(t.match.back(), m, std::regex(e)))
                      {
                        invalid = false;
                        // std::cerr << "ArgValid\nstr\n" << t.match.back() << "\n\n";
                      }
                    }
                    if (invalid)
                    {
                      goto invalid_arg;
                    }
                  }
                  else if (s.find_first_of("\'") != std::string::npos)
                  {
                    // str
                    // std::cerr << "Str\n";
                    t.match.emplace_back("");
                    ++j; // skip start quote
                    bool escaped {false};
                    for (;j < t.args.size(); ++j)
                    {
                      if (! escaped && t.args.at(j) == '\'')
                      {
                        // skip end quote
                        break;
                      }
                      if (t.args.at(j) == '\\')
                      {
                        escaped = true;
                        continue;
                      }
                      if (escaped)
                      {
                        t.match.back() += "\\";
                        t.match.back() += t.args.at(j);
                        escaped = false;
                        continue;
                      }
                      t.match.back() += t.args.at(j);
                    }
                    // std::cerr << "Arg-Str\n" << t.match.back() << "\n";
                    bool invalid {true};
                    std::string mstr {"'" + t.match.back() + "'"};
                    for (auto const& e : reg_str)
                    {
                      std::smatch m;
                      if (std::regex_match(mstr, m, std::regex(e)))
                      {
                        invalid = false;
                        // std::cerr << "ArgValid\nstr\n" << t.match.back() << "\n\n";
                      }
                    }
                    if (invalid)
                    {
                      goto invalid_arg;
                    }
                  }
                  else if (s.find_first_of("`") != std::string::npos)
                  {
                    // literal
                    // std::cerr << "Str\n";
                    t.match.emplace_back("");
                    ++j; // skip start quote
                    bool escaped {false};
                    for (;j < t.args.size(); ++j)
                    {
                      if (! escaped && t.args.at(j) == '`')
                      {
                        // skip end quote
                        break;
                      }
                      if (t.args.at(j) == '\\')
                      {
                        escaped = true;
                        continue;
                      }
                      if (escaped)
                      {
                        t.match.back() += "\\";
                        t.match.back() += t.args.at(j);
                        escaped = false;
                        continue;
                      }
                      t.match.back() += t.args.at(j);
                    }
                    // std::cerr << "Arg-Str\n" << t.match.back() << "\n";
                    bool invalid {true};
                    for (auto const& e : reg_str)
                    {
                      std::smatch m;
                      if (std::regex_match(t.match.back(), m, std::regex(e)))
                      {
                        invalid = false;
                        // std::cerr << "ArgValid\nstr\n" << t.match.back() << "\n\n";
                      }
                    }
                    if (invalid)
                    {
                      goto invalid_arg;
                    }
                  }
                  else
                  {
invalid_arg:
                    // invalid arg
                    if (settings_.readline)
                    {
                      std::cerr << error(t, _ifile, "invalid argument", "");
                      std::cerr
                      << aec::wrap(t.match.back(), aec::fg_magenta)
                      << "\n";

                      stk = std::stack<Tmacro>();
                      break;
                    }
                    throw std::runtime_error("macro " + t.name + " has an invalid argument");
                  }
                }
                // std::cerr << "ArgValid\ncomplete\n\n";
              }
              else
              {
                bool invalid_regex {true};
                if (it->second.rx_fn.size() == 1)
                {
                  std::smatch match;
                  if (std::regex_match(t.args, match, std::regex(it->second.rx_fn.at(0).first)))
                  {
                    invalid_regex = false;
                    for (auto const& e : match)
                    {
                      t.match.emplace_back(std::string(e));
                    }
                  }
                }
                else
                {
                  std::size_t index {0};
                  for (auto const& rf : it->second.rx_fn)
                  {
                    std::smatch match;
                    if (std::regex_match(t.args, match, std::regex(rf.first)))
                    {
                      invalid_regex = false;
                      for (auto const& e : match)
                      {
                        t.match.emplace_back(std::string(e));
                        if (settings_.debug)
                        {
                          std::cerr << "arg: " << std::string(e) << "\n";
                        }
                      }
                      t.fn_index = index;
                      break;
                    }
                    ++index;
                  }
                }
                if (invalid_regex)
                {
                  // TODO fix, correct args not being listed out
                  std::cerr << error(t, _ifile, "invalid argument", "");
                  std::cerr
                  << delim_start_ << " "
                  << aec::wrap(t.name, aec::fg_white) << " "
                  << aec::wrap(it->second.usage, aec::fg_green) << " "
                  << delim_end_
                  << "\n";
                  for (auto const& rf : it->second.rx_fn)
                  {
                    std::cerr << aec::wrap(rf.first, aec::fg_magenta) << "\n";
                  }
                  if (settings_.readline)
                  {
                    stk = std::stack<Tmacro>();
                    break;
                  }
                  throw std::runtime_error("invalid argument");
                }
              }

              // process macro
              int ec {0};
              Ctx ctx {t.res, t.match, "", nullptr};
              try
              {
                // ignore matching names
                std::smatch match;
                if (std::regex_match(t.name, match, std::regex(ignore_)))
                {
                  ++stats_.ignored;
                }

                // call core
                else if (it->second.type == Mtype::core)
                {
                  ++stats_.macro;
                  ctx.core = std::make_unique<Core_Ctx>(buf, r, w, _ifile, _ofile);
                  ec = run_internal(it->second.rx_fn.at(t.fn_index).second, ctx);
                }

                // call internal
                else if (it->second.type == Mtype::internal)
                {
                  ++stats_.macro;
                  ec = run_internal(it->second.rx_fn.at(t.fn_index).second, ctx);
                }

                // call remote
                else if (it->second.type == Mtype::remote)
                {
                  ++stats_.macro;
                  ec = run_remote(it->second, ctx);
                }

                // call external
                else if (it->second.type == Mtype::external)
                {
                  ++stats_.macro;
                  ec = run_external(it->second, ctx);
                }
              }
              catch (std::exception const& e)
              {
                std::cerr << error(t, _ifile, "macro failed", e.what());
                if (settings_.readline)
                {
                  i += delim_end_.size() - 1;
                  stk = std::stack<Tmacro>();
                  continue;
                }
                throw std::runtime_error("macro failed");
              }
              if (ec != 0)
              {
                std::cerr << error(t, _ifile, "macro failed", ctx.err_msg);
                if (settings_.readline)
                {
                  i += delim_end_.size() - 1;
                  stk = std::stack<Tmacro>();
                  continue;
                }
                throw std::runtime_error("macro failed");
              }

              // find and replace macro words
              run_hooks(h_res_, t.res);

              // debug
              if (settings_.debug)
              {
                std::cerr << "\nRes:\n~" << t.res << "~\n";
              }

              if (t.res.find(delim_start_) != std::string::npos)
              {
                // remove escaped nl chars
                // t.res = OB::String::replace_all(t.res, "\\\n", "");

                // remove all nl chars that follow delim_end
                // this would normally be removed by the reader
                // t.res = OB::String::replace_all(t.res, delim_end_ + "\n", delim_end_);

                // TODO handle the same as if it was read from file
                line.insert(i + delim_end_.size(), t.res);
                i += delim_end_.size() - 1;
                continue;
              }

              if (! stk.empty())
              {
                stk.top().str += t.res;

                if ((! t.res.empty()) && (i + delim_end_.size() - 1 == line.size() - 1))
                {
                  // account for when end delim is last char on line
                  // add a newline char to buf
                  // only if response is not empty
                  stk.top().str += "\n";
                }
              }
              else
              {
                // add indentation
                {
                  std::string indent_str (indent, indent_char);
                  t.res = OB::String::replace_all(t.res, "\n", "\n" + indent_str);
                  t.res = OB::String::replace_last(t.res, "\n" + indent_str, "\n");
                  t.res = OB::String::replace_all(t.res, "\n" + indent_str + "\n", "\n\n");
                }

                buf += t.res;
                // std::cerr << "t.name: " << t.name << "\n";
                // std::cerr << "i: " << i << "\n";
                // std::cerr << "t.res: " << t.res << "\n";

                if ((! t.res.empty()) && (i + delim_end_.size() - 1 == line.size() - 1))
                {
                  // account for when end delim is last char on line
                  // add a newline char to buf
                  // only if response is not empty
                  buf += "\n";
                }
              }
            }

            // debug
            if (settings_.debug)
            {
              std::cerr << "\nRes fmt:\n~" << t.res << "~\n";
              std::cerr << "\nBuf fmt:\n~" << buf << "~\n";
            }

            if (stk.empty())
            {
              ast.emplace_back(t);
            }
            else
            {
              auto& l = stk.top();
              l.children.emplace_back(t);
            }
          }

          i += delim_end_.size() - 1;
          continue;
        }
      }

regular_char:

      // case else other characters
      if (! stk.empty())
      {
        // inside macro
        auto& t = stk.top();

        if (i == (line.size() - 1))
        {
          if (line.at(i) != '\\')
          {
            t.str += line.at(i);
            t.str += "\n";
          }
        }
        else
        {
          t.str += line.at(i);
        }
      }
      else
      {
        // outside macro
        // append to output buffer
        if (settings_.copy)
        {
          if (i == (line.size() - 1))
          {
            if (line.at(i) != '\\')
            {
              buf += line.at(i);
              buf += "\n";
            }
          }
          else
          {
            buf += line.at(i);
          }
        }
      }

    }

    // find and replace macro words
    run_hooks(h_end_, buf);

    if (settings_.debug)
    {
      if (! ast_.ast.empty())
      {
        std::cerr << "AST:\n" << ast_.str() << "\n";
      }
      ast_.clear();
    }

    if (buf.empty() || buf == "\n")
    {
      continue;
    }

    // append buf to output file
    w.write(buf);
  }

  if (! stk.empty())
  {
    auto t = Tmacro();
    t.name = delim_end_;
    t.line_start = r.row();
    t.line_end = r.row();
    t.begin = r.line().size();
    std::cerr << error(t, _ifile, "missing closing delimiter", r.line());
    throw std::runtime_error("missing closing delimiter");
  }

  if (! _ofile.empty())
  {
    w.close();
  }
}

int M8::run_internal(macro_fn const& func, Ctx& ctx)
{
  ++stats_.internal;

  return func(ctx);
}

int M8::run_external(Macro const& macro, Ctx& ctx)
{
  ++stats_.external;

  std::string m_args;
  for (std::size_t i = 1; i < ctx.args.size(); ++i)
  {
    m_args += ctx.args[i];
    if (i < ctx.args.size() - 1)
      m_args += " ";
  }
  return OB::exec(ctx.str, macro.name + " " + m_args);
}

int M8::run_remote(Macro const& macro, Ctx& ctx)
{
  ++stats_.remote;

  Http api;
  api.req.method = "POST";
  api.req.headers.emplace_back("content-type: application/json");
  api.req.url = macro.url;

  Json data;
  data["name"] = macro.name;
  data["args"] = ctx.args;
  api.req.data = data.dump();

  std::cout << "Remote macro call -> " << macro.name << "\n";
  std::future<int> send {std::async(std::launch::async, [&]() {
    api.run();
    int status_code = api.res.status;
    if (status_code != 200)
    {
      return -1;
    }
    else
    {
      ctx.str = api.res.body;
      return 0;
    }
  })};

  std::future_status fstatus;
  do
  {
    fstatus = send.wait_for(std::chrono::milliseconds(250));
    std::cout << "." << std::flush;
  }
  while (fstatus != std::future_status::ready);

  int ec = send.get();
  if (ec == 0)
  {
    std::cout << "\033[2K\r" << "Success: remote call\n";
  }
  else
  {
    std::cout << "\033[2K\r" << "Error: remote call\n";
  }

  return ec;
}

std::string M8::env_var(std::string const& str) const
{
  std::string res;
  if (const char* envar = std::getenv(str.c_str()))
  {
    res = envar;
  }
  return res;
}