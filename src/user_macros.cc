#include "user_macros.hh"
#include "m8.hh"

#include <string>
#include <sstream>
#include <iostream>

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

namespace Macros
{

void user_macros(M8& m8)
{
  // define macros
  // M8::set_macro(name, info, usage, regex, func)

  // m8.set_macro("",
  //   "",
  //   "",
  //   "^(.*)$",
  //   [&](auto& ctx) {
  //   var str = ctx.args.at(1);
  //   ctx.str = str;
  //   return 0;
  //   });
}

} // namespace Macros
