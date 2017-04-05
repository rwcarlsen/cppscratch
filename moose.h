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

// Defines a value/property computed by calling a specified (lambda) function and then returning
// the value stored at a particular address/pointer location.  Caches the most recent
// location evaluated since the last shift() call.  If the location is the same for later calls,
// the (lambda) function is not called again and just the value at the variable address is
// returned.  shift() calls to the holding QpStore reset the cached value.  This is useful for
// avoiding duplicate computations if the lambda function call results in more than one
// value/property being computed.  This allows accomodates single "material" classes that want to
// define/calculate several properties that may depend on each other.
template <typename T>
class LambdaVarValuer : public QpValuer<T>
{
public:
  virtual ~LambdaVarValuer() {}
  void init(T * var, std::function<void(const Location &)> func)
  {
    _var = var;
    _func = func;
  }
  virtual T get(const Location & loc) override
  {
    if (!_prev_loc || *_prev_loc != loc)
      _func(loc);
    _prev_loc = new Location(loc);
    return *_var;
  }

  virtual void shift() override { _prev_loc = nullptr; }

private:
  std::function<void(const Location &)> _func;
  T * _var;
  Location * _prev_loc = nullptr;
};

template <typename T>
class LambdaValuer : public QpValuer<T>
{
public:
  virtual ~LambdaValuer() {}
  void init(std::function<T(const Location &)> func) { _func = func; }
  virtual T get(const Location & loc) override { return _func(loc); }
private:
  std::function<T(const Location &)> _func;
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
