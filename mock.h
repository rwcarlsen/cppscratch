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
  Material(FEProblem & fep) : _props(fep.store()) {}

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
    _props.addProp(valuer, name, true);
  }

protected:
  QpStore & _props;
};

#define bind_prop_func(prop, func)                                                                 \
  addPropFunc(prop, [this](const Location & loc) { return func(loc); })
#define bind_prop_func_var(prop, func, var)                                                        \
  addPropFuncVar([this](const Location & loc) {                                                    \
    func(loc);                                                                                     \
    return var;                                                                                    \
  })

