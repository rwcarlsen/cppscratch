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

template <typename T>
class GuaranteeSet : public Valuer<T>
{
public:
  GuaranteeSet(std::set<std::string> guarantees) : _guarantees(guarantees) {}
  virtual bool guarantees(std::string val) { return _guarantees.count(val) > 0; }
private:
  std::set<std::string> _guarantees;
};

// Defines a value/property computed by calling a specified (lambda) function and then returning
// the value stored at a particular address/pointer location.  Caches the most recent
// location evaluated since the last shift() call.  If the location is the same for later calls,
// the (lambda) function is not called again and just the value at the variable address is
// returned.  shift() calls to the holding QpStore reset the cached value.  This is useful for
// avoiding duplicate computations if the lambda function call results in more than one
// value/property being computed.  This allows/accomodates single "material" classes that want to
// calculate several properties at once that may depend on each other.
template <typename T>
class LambdaVarValuer : public GuaranteeSet<T>
{
public:
  LambdaVarValuer(std::set<std::string> guarantees = {}) : GuaranteeSet<T>(guarantees) {}
  virtual ~LambdaVarValuer() {}
  void init(T * var, std::function<void(const Location &)> func)
  {
    _var = var;
    _func = func;
  }
  virtual T get(const Location & loc) override
  {
    if (!_prev_loc || *_prev_loc != loc)
    {
      _func(loc);
      _prev_loc = new Location(loc);
    }
    return *_var;
  }

  virtual void shift() override { _prev_loc = nullptr; }

private:
  std::function<void(const Location &)> _func;
  T * _var;
  Location * _prev_loc = nullptr;
};

template <typename T>
class LambdaValuer : public GuaranteeSet<T>
{
public:
  LambdaValuer(std::set<std::string> guarantees = {}) : GuaranteeSet<T>(guarantees) {}
  virtual ~LambdaValuer() {}
  void init(std::function<T(const Location &)> func) { _func = func; }
  virtual T get(const Location & loc) override { return _func(loc); }
private:
  std::function<T(const Location &)> _func;
};

// Since a single material property might be used several times by different things at a single
// time-step,mesh-loc (relatively) consecutively, we can avoid recomputing each of these times by
// wrapping the valuers representing those properties with a CacheValuer.  You could even
// configure the ValueStore to wrap them automatically with something like this.
template <typename T>
class CacheValuer : public Valuer<T>
{
public:
  CacheValuer(Valuer<T> * v) : _valuer(v) {}
  virtual T get(const Location & loc) override
  {
    if (!_prev_loc || *_prev_loc != loc)
    {
      _cache = _valuer->get(loc);
      _prev_loc = new Location(loc);
    }
    return _cache;
  }

  virtual void shift() override { _prev_loc = nullptr; }
private:
  Location * _prev_loc = nullptr;
  Valuer<T> * _valuer;
  T _cache;
};

// Demonstrates how we might replicate current material property access patterns.
class MaterialPropertyInterface
{
public:
  MaterialPropertyInterface(FEProblem & fep) : _fep(fep) {}
  template <typename T>
  T prop(const std::string & name)
  {
    // TODO: auto-generate/discover location from FEProblem.assembly() or similar
    return _fep.props().get<T>(name, Location(0, 0, 0));
  }

private:
  FEProblem & _fep;
};

class Material
{
public:
  Material(FEProblem & fep, std::set<BlockId> blocks = {}) : _props(fep.props()), _blocks(blocks) {}

  template <typename T>
  T prop(const std::string & name, const Location & loc, std::vector<std::string> needs = {})
  {
    return _props.get<T>(name, loc, needs);
  }

  template <typename T>
  void addPropFunc(std::string name,
                   std::function<T(const Location &)> func,
                   std::set<std::string> guarantees = {})
  {
    auto valuer = new LambdaValuer<T>(guarantees);
    valuer->init(func);

    if (_blocks.size() == 0)
      _props.add(name, valuer, true);
    else
    {
      // NOTE: while you *can* do something like this, it is much less necessary.  For cases when
      // you are trying to improve performance by only evaluating the property on locations where
      // it is necessary - that already happens automagically with this architecture.  For cases
      // where you want to split the mesh domain and map a single property name to multiple
      // material/property objects, it may be more clear to have that entire mapping in one place
      // e.g.  via the Umbrella material class rather than scattered around in the config of
      // several material objects (i.e. the current Materials' "blocks='0 1, etc.'" config).
      ValId id = _props.add(name + "__inner", valuer, true);
      _props.addMapper(name, [this, id, name](const Location & loc) {
        if (_blocks.count(loc.block_id) > 0)
          return id;
        throw std::runtime_error("property '" + name + "' is not defined on block " +
                                 std::to_string(loc.block_id));
      });
    }
  }
  template <typename T>
  void addPropFuncVar(std::string name,
                      T * var,
                      std::function<void(const Location &)> func,
                      std::set<std::string> guarantees = {})
  {
    auto valuer = new LambdaVarValuer<T>(guarantees);
    valuer->init(var, func);

    if (_blocks.size() == 0)
      _props.add(name, valuer, true);
    else
    {
      // NOTE: while you *can* do something like this, it is much less necessary.  For cases when
      // you are trying to improve performance by only evaluating the property on locations where
      // it is necessary - that already happens automagically with this architecture.  For cases
      // where you want to split the mesh domain and map a single property name to multiple
      // material/property objects, it may be more clear to have that entire mapping in one place
      // e.g.  via the Umbrella material class rather than scattered around in the config of
      // several material objects (i.e. the current Materials' "blocks='0 1, etc.'" config).
      ValId id = _props.add(name + "__inner", valuer, true);
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

#define bind_prop_func(prop, func, T, ...)                                                         \
  addPropFunc<T>(prop, [this](const Location & loc) { return func(loc); }, {__VA_ARGS__})
#define bind_prop(prop, func, var, ...)                                                            \
  addPropFuncVar(prop, &var, [this](const Location & loc) { func(loc); }, {__VA_ARGS__})

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
