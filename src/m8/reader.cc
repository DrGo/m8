#include "m8/reader.hh"

#include "ob/term.hh"
namespace aec = OB::Term::ANSI_Escape_Codes;

#include "lib/linenoise.hh"

#include <cctype>
#include <cstdint>
#include <cstddef>

#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <vector>
#include <map>

#include <filesystem>
namespace fs = std::filesystem;

Reader::Reader()
{
  if (readline_)
  {
    auto const fn_file = [](std::string file) {
      auto const tilde = file.find_first_of("~");
      if (tilde != std::string::npos)
      {
        std::string home {std::getenv("HOME")};
        if (home.empty())
        {
          throw std::runtime_error("could not open the input file");
        }
        file.replace(tilde, 1, home);
      }
      return file;
    };

    history_ = fn_file(history_);
    linenoise::LoadHistory(history_.c_str());
    linenoise::SetMultiLine(true);
    linenoise::SetHistoryMaxLen(1000);
    linenoise::SetCompletionCallback([](const char* editBuffer, std::vector<std::string>& completions) {
      // if (editBuffer[0] == 'a')
      // {
      //   completions.push_back("all");
      //   completions.push_back("alli");
      // }
    });
  }
}

Reader::~Reader()
{
  if (readline_)
  {
    linenoise::SaveHistory(history_.c_str());
  }
}

void Reader::open(std::string const& file_name)
{
  ifile_.open(file_name);
  if (! ifile_.is_open())
  {
    throw std::runtime_error("could not open the input file");
  }
  lines_[0] = 0;
  readline_ = false;
}

std::string Reader::line()
{
  return line_;
}

bool Reader::next(std::string& str)
{
  ++row_;
  lines_[row_] = ifile_.tellg();
  bool status {false};

  if (readline_)
  {
    bool quit {false};
    prompt_ = aec::wrap("M8[", aec::fg_magenta) + aec::wrap(std::to_string(row_), aec::fg_green) + aec::wrap("]>", aec::fg_magenta) + " ";
    std::string input = linenoise::Readline(prompt_.c_str(), quit);

    if (input == ".quit" || input == ".quit" || quit)
    {
      std::cout << "\n";
      --row_;
      status = false;
    }
    else
    {
      linenoise::AddHistory(input.c_str());
      str = input;
      line_ = str;
      status = true;
    }

    return status;
  }
  else
  {
    if (std::getline(ifile_, str))
    {
      line_ = str;
      status = true;
    }
    else
    {
      --row_;
      status = false;
    }

    return status;
  }
}

std::uint32_t Reader::row()
{
  return row_;
}

std::uint32_t Reader::col()
{
  return col_;
}
