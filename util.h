
#include <set>
#include <string>
#include <map>

#include "mock.h"

// Generate a standardized derivative property name using a base name plus an (ordered) sequence of
// independent variable names of each partial derivative.  1 varaible implies 1st order
// derivative, 2 variables is a second order derivative, etc.
template <typename T, typename... Args>
std::string
derivProp(std::string prop_name, T val, Args... independent_vars)
{
  std::stringstream ss;
  ss << prop_name << "_D" << val;
  return ss.str() + derivProp("", independent_vars...);
}

// Make this private only in a .C file.
std::string
derivProp(std::string prop_name)
{
  return "";
}

// Convenience class for mapping one property (name/id) to several (sub) materials depending on
// the block id.
class Umbrella : public Material
{
public:
  Umbrella(FEProblem & fep,
           std::string prop_name,
           std::map<std::string, std::set<unsigned int>> subprops)
    : Material(fep)
  {
    fep.registerMapper(prop_name, [&fep, prop_name, subprops](const Location & loc) {
      for (auto & it : subprops)
      {
        if (it.second.count(loc.block()) > 0)
          return fep.prop_id(it.first);
      }
      throw std::runtime_error("property " + prop_name + " is not defined on block " +
                               std::to_string(loc.block()));
    });
  }
};

