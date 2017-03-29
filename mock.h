#pragma once

#include <set>
#include <string>
#include <sstream>

#include "valuer.h"

class FEProblem
{
public:
  FEProblem(bool cyclical_detection = false) : _propstore(cyclical_detection) {}

  template <typename T>
  inline unsigned int registerProp(QpValuer<T> * v, const std::string & prop, bool take_ownership = false)
  {
    return _propstore.registerValue<T>(v, prop, take_ownership);
  }

  inline unsigned int registerMapper(const std::string & prop, std::function<unsigned int(const Location&)> mapper)
  {
    return _propstore.registerMapper(prop, mapper);
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

  template <typename T>
  void registerPropFunc(std::string name, std::function<T(const Location&)> func)
  {
    auto valuer = new LambdaValuer<T>();
    valuer->init(func);
    _fep.registerProp(valuer, name, true);
  }
  template <typename T>
  void registerPropFuncVar(std::string name, T* var, std::function<void(const Location&)> func)
  {
    auto valuer = new LambdaVarValuer<T>();
    valuer->init(var, func);
    _fep.registerProp(valuer, name, true);
  }
protected:
  FEProblem& _fep;
};

#define bind_prop_func(prop, func) registerPropFunc(prop, [this](const Location& loc){return func(loc);})
#define bind_prop_func_var(prop, func, var) registerPropFuncVar([this](const Location& loc){func(loc); return var;})

