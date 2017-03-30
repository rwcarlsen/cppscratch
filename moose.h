#pragma once

#include <set>
#include <map>
#include <string>
#include <sstream>

#include "valuer.h"

class FEProblem
{
public:
  FEProblem(bool errcheck = false) : _props(errcheck) {}

  QpStore & props() { return _props; }

private:
  QpStore _props;
};

class Material
{
public:
  Material(FEProblem & fep) : _props(fep.props()) {}

  template <typename T>
  void addPropFunc(std::string name, std::function<T(const Location &)> func)
  {
    auto valuer = new LambdaValuer<T>();
    valuer->init(func);
    _props.add(valuer, name, true);
  }
  template <typename T>
  void addPropFuncVar(std::string name, T * var, std::function<void(const Location &)> func)
  {
    auto valuer = new LambdaVarValuer<T>();
    valuer->init(var, func);
    _props.add(valuer, name, true);
  }

protected:
  QpStore & _props;
};

#define bind_prop_func(prop, T, func)                                                              \
  addPropFunc<T>(prop, [this](const Location & loc) { return func(loc); })
#define bind_prop_func_var(prop, T, func, var)                                                     \
  addPropFuncVar<T>(prop, &var, [this](const Location & loc) { func(loc); })

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
    _props.addMapper(prop_name, [this, prop_name, subprops](const Location & loc) {
      for (auto & it : subprops)
      {
        if (it.second.count(loc.block_id) > 0)
          return _props.id(it.first);
      }
      throw std::runtime_error("property " + prop_name + " is not defined on block " +
                               std::to_string(loc.block_id));
    });
  }
};
