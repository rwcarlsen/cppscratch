#pragma once

#include <string>

#include "valuer.h"

class FEProblem
{
public:
  FEProblem(bool cyclical_detection = false) : _propstore(cyclical_detection) {}

  template <typename T>
  inline unsigned int registerProp(QpValuer<T> * v, const std::string & prop)
  {
    return _propstore.registerValue<T>(v, prop);
  }

  template <typename T>
  inline T getProp(const std::string & name, const Location & loc)
  {
    return _propstore.value<T>(name, loc);
  }
  template <typename T>
  inline T getProp(unsigned int prop, const Location & loc)
  {
    return _propstore.value<T>(prop, loc);
  }

  template <typename T>
  inline T getPropOld(const std::string & name, const Location & loc)
  {
    return _propstore.oldValue<T>(name, loc);
  }
  template <typename T>
  inline T getPropOld(unsigned int prop, const Location & loc)
  {
    return _propstore.oldValue<T>(prop, loc);
  }

  inline unsigned int prop_id(const std::string & name) { return _propstore.id(name); }

  void shift() { _propstore.shift(); }

private:
  QpStore _propstore;
};

class Material
{
public:
  Material(FEProblem & fep) : _fep(fep) {}
  ~Material()
  {
    for (auto func : _delete_funcs)
      func();
  }
  template <typename T>
  void registerPropFunc(std::string name, std::function<T(const Location&)> func)
  {
    auto valuer = new LambdaValuer<T>();
    valuer->init(func);
    _delete_funcs.push_back([=](){delete valuer;});
    _fep.registerProp(valuer, name);
  }
  template <typename T>
  void registerPropFuncVar(std::string name, T* var, std::function<void(const Location&)> func)
  {
    auto valuer = new LambdaVarValuer<T>();
    valuer->init(var, func);
    _delete_funcs.push_back([=](){delete valuer;});
    _fep.registerProp(valuer, name);
  }
private:
  FEProblem& _fep;
  std::vector<std::function<void()>> _delete_funcs;
};

#define bind_mat_prop(prop, func) registerPropFunc(prop, [this](const Location& loc){return func(loc);})
#define bind_mat_prop_var(prop, func, var) registerPropFuncVar([this](const Location& loc){func(loc); return var;})

