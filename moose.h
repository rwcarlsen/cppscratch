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
class LambdaVarValuer : public Valuer<T>
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
class LambdaValuer : public Valuer<T>
{
public:
  virtual ~LambdaValuer() {}
  void init(std::function<T(const Location &)> func) { _func = func; }
  virtual T get(const Location & loc) override { return _func(loc); }
private:
  std::function<T(const Location &)> _func;
};

class MaterialPropertyInterface
{
public:
  MaterialPropertyInterface(FEProblem & fep) : _fep(fep) {}
  // TODO: auto-generate Location/loc from FEProblem.assembly()
  template <typename T>
  T prop(const std::string & name)
  {
    return _fep.props().get<T>(name, Location(0, 0, 0));
  }

private:
  FEProblem & _fep;
};

class Material
{
public:
  Material(FEProblem & fep, std::set<BlockId> blocks = {})
    : _props(fep.props()), _blocks(blocks)
  {
  }

  template <typename T>
  T prop(const std::string & name, const Location & loc)
  {
    return _props.get<T>(name, loc);
  }

  template <typename T>
  void addPropFunc(std::string name, std::function<T(const Location &)> func)
  {
    auto valuer = new LambdaValuer<T>();
    valuer->init(func);

    if (_blocks.size() == 0)
      _props.add(valuer, name, true);
    else
    {
      // NOTE: while you *can* do something like this, we don't really need it.  For cases when
      // you are trying to improve performance by only evaluating the property on locations where
      // it is necessary - that already happens automagically with this architecture.  For cases
      // where you want to split the mesh domain and map a single property name to multiple
      // material/property objects, it is more clear to have that entire mapping in one place e.g.
      // via the Umbrella material class rather than scattered around in the config of several
      // material objects (i.e. the current Materials' "blocks='0 1, etc.'" config).
      ValId id = _props.add(valuer, name + "__inner", true);
      _props.addMapper(name, [this, id, name](const Location & loc) {
        if (_blocks.count(loc.block_id) > 0)
          return id;
        throw std::runtime_error("property '" + name + "' is not defined on block " +
                                 std::to_string(loc.block_id));
      });
    }
  }
  template <typename T>
  void addPropFuncVar(std::string name, T * var, std::function<void(const Location &)> func)
  {
    auto valuer = new LambdaVarValuer<T>();
    valuer->init(var, func);

    if (_blocks.size() == 0)
      _props.add(valuer, name, true);
    else
    {
      // NOTE: while you *can* do something like this, we don't really need it.  For cases when
      // you are trying to improve performance by only evaluating the property on locations where
      // it is necessary - that already happens automagically with this architecture.  For cases
      // where you want to split the mesh domain and map a single property name to multiple
      // material/property objects, it is more clear to have that entire mapping in one place e.g.
      // via the Umbrella material class rather than scattered around in the config of several
      // material objects (i.e. the current Materials' "blocks='0 1, etc.'" config).
      ValId id = _props.add(valuer, name + "__inner", true);
      _props.addMapper(name, [this, id, name](const Location & loc) {
        if (_blocks.count(loc.block_id) > 0)
          return id;
        throw std::runtime_error("property '" + name + "' is not defined on block " +
                                 std::to_string(loc.block_id));
      });
    }
  }

protected:
  QpStore & _props;

private:
  std::set<BlockId> _blocks;
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
           std::map<std::string, std::set<BlockId>> subprops)
    : Material(fep)
  {
    _props.addMapper(prop_name, [this, prop_name, subprops](const Location & loc) {
      for (auto & it : subprops)
      {
        if (it.second.count(loc.block_id) > 0)
          return _props.id(it.first);
      }
      throw std::runtime_error("property '" + prop_name + "' is not defined on block " +
                               std::to_string(loc.block_id));
    });
  }
};
