#pragma once

#include <set>
#include <string>
#include <sstream>

#include "valuer.h"

class FEProblem
{
public:
  FEProblem(bool cyclical_detection = false) : _props(cyclical_detection) {}

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

