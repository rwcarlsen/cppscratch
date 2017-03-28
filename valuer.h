#pragma once

#include <map>
#include <vector>
#include <list>
#include <string>

class FEProblem;
class Location;

template <typename T>
class QpValuer
{
public:
  virtual T value(const Location &) = 0;
};

template <typename T>
class LambdaVarValuer : public QpValuer<T>
{
public:
  void init(T* var, std::function<void(const Location &)> func) { _var = var; _func = func; }
  virtual T value(const Location & loc) override { _func(loc); return _var;}
private:
  std::function<void(const Location &)> _func;
  T* _var;
};

template <typename T>
class LambdaValuer : public QpValuer<T>
{
public:
  void init(std::function<T(const Location &)> func) { _func = func; }
  virtual T value(const Location & loc) override { return _func(loc); }
private:
  std::function<T(const Location &)> _func;
};

class Location
{
public:
  Location(FEProblem & fep,
           unsigned int nqp,
           unsigned int qp,
           unsigned int elem = 1,
           unsigned int parent_id = 0,
           unsigned int face_id = 0)
    : _nqp(nqp), _qp(qp), _fep(fep), _elem_parent_id(parent_id), _elem_id(elem), _face_id(face_id)
  {
  }
  unsigned int qp() const { return _qp; }
  unsigned int nqp() const { return _nqp; }
  unsigned int elem_id() const {return _elem_id;}
  FEProblem & fep() const { return _fep; }
  inline Location parent() const
  {
    return Location(_fep, _nqp, _qp, _elem_id, _elem_parent_id);
  }

  friend bool operator<(const Location & lhs, const Location & rhs)
  {
    return lhs._elem_id < rhs._elem_id || lhs._face_id < rhs._face_id || lhs._qp < rhs._qp;
  }

private:
  unsigned int _elem_id;
  unsigned int _elem_parent_id;
  unsigned int _face_id;
  unsigned int _qp;

  unsigned int _nqp;
  FEProblem & _fep;
};

class QpStore
{
public:
  QpStore(bool errcheck = false) : _errcheck(errcheck), _cycle_stack(1, {}) {};

  inline unsigned int id(const std::string & name)
  {
    if (_ids.count(name) == 0)
      throw std::runtime_error("value " + name + " doesn't exist");
    return _ids[name];
  }

  template <typename T>
  unsigned int registerValue(QpValuer<T> * q, const std::string & name)
  {
    unsigned int id = _valuers.size();
    _ids[name] = id;
    _valuers.push_back(q);
    _want_old.push_back(false);
    _types.push_back(typeid(T).hash_code());
    _type_names.push_back(typeid(T).name());
    return id;
  }

  template <typename T>
  inline void checkType(unsigned int id)
  {
    if (typeid(T).hash_code() != _types[id])
      throw std::runtime_error("wrong type requested: " + _type_names[id] + " != " +
                               typeid(T).name());
  }

  template <typename T>
  T value(unsigned int id, const Location & loc)
  {
    if (_errcheck)
    {
      if (_cycle_stack.back().count(id) > 0)
        throw std::runtime_error("cyclical value dependency detected");
      _cycle_stack.back()[id] = true;
      checkType<T>(id);
    }

    // mark this property as computed if we need its old value
    if (_want_old[id])
      _external_curr[id][loc] = true;

    auto val = static_cast<QpValuer<T> *>(_valuers[id])->value(loc);
    if (_want_old[id])
      stageOldVal(id, loc, val);
    if (_errcheck)
      _cycle_stack.back().erase(id);
    return val;
  }

  template <typename T>
  inline double value(const std::string & name, const Location & loc)
  {
    return value<T>(id(name), loc);
  }

  template <typename T>
  T oldValue(unsigned int id, const Location & loc)
  {
    if (_errcheck)
    {
      _cycle_stack.push_back({});
      checkType<T>(id);
    }

    if (!_want_old[id])
    {
      _want_old[id] = true;
      _delete_funcs[id] = [](void * val) { delete reinterpret_cast<T *>(&val); };
    }

    // force computation of current value in preparation for next old value if there was no other
    // explicit calls to value for this property/location combo.
    if (!_external_curr[id][loc])
    {
      value<T>(id, loc);
      // reset to false because above value<>(...) call sets it to true, but we only want it to be
      // true if value is called by someone else (i.e. externally).
      _external_curr[id][loc] = false;
      if (_errcheck)
        _cycle_stack.pop_back();
    }

    if (_old_vals[id].count(loc) > 0)
      return *static_cast<T *>(_old_vals[id][loc]);

    // There was no previous old value, so we use the zero/default value.  We also need to
    // stage/store if there is no corresponding stored current value to become the next old value.
    T val{};
    if (_curr_vals[id].count(loc) > 0 )
      stageOldVal(id, loc, val);
    return val;
  }

  template <typename T>
  T oldValue(const std::string & name, const Location & loc)
  {
    return oldValue<T>(id(name), loc);
  }

  // Projects/copies computed old values at the source locations to live under destination
  // locations (mapped one to one in the ordered vectors).  When doing e.g. mesh adaptivity, call
  // this to project values at old locations (srcs) to new locations (dsts) where they were never
  // explicitly computed before.  This needs to be called *after* the call to shift and *before*
  // calls to oldValue.
  void project(std::vector<const Location*> srcs, std::vector<const Location*> dsts)
  {
    for (unsigned int id = 0; id < _valuers.size(); id++)
    {
      for (int i = 0; i < srcs.size(); i++)
      {
        auto& src = *srcs[i];
        auto& dst = *dsts[i];
        _delete_funcs[id](_old_vals[id][dst]);
        _old_vals[id][dst] = _old_vals[id][src];
        _delete_funcs[id](_old_vals[id][src]);
      }
    }
  }

  // Moves stored "current" values to "older" values, discarding any previous "older" values.
  void shift() { _old_vals.swap(_curr_vals); }

private:
  // Stores/saves a computed value so it can be used as old next iteration/step (i.e. after shift
  // call).
  template <typename T>
  void stageOldVal(unsigned int id, const Location & loc, const T & val)
  {
    auto prev = _curr_vals[id][loc];
    if (prev != nullptr)
      delete static_cast<T *>(prev);
    _curr_vals[id][loc] = new T(val);
  }

  // map<value_name, value_id>
  std::map<std::string, unsigned int> _ids;
  // map<value_id, valuer>
  std::vector<void *> _valuers;
  // map<value_id, want_old>. True if an old version of the value has (ever) been requested.
  std::vector<bool> _want_old;
  // map<value_id, type_id> Stores a unique type id corresponding to each value.  Used for error
  // checking.
  std::vector<size_t> _types;
  // map<value_id, value_name>
  std::vector<std::string> _type_names;


  // map<value_id, map<[elem_id,face_id,quad-point,etc], val>>>.
  // Caches any computed/retrieved values for which old values are needed.
  std::map<unsigned int, std::map<Location, void*>> _curr_vals;
  // Stores needed/requested old values.
  std::map<unsigned int, std::map<Location, void*>> _old_vals;

  // map<value_id, map<[elem_id,face_id,quad-point,etc], _external_curr>>
  // Stores whether or not the value function is ever called externally (from outside the QpStore
  // class).  If this is never marked true, then oldValue needs to invoke evaluation of the
  // current values on its own.
  std::map<unsigned int, std::map<Location, bool>> _external_curr;
  // map<value_id, map<[elem_id,face_id,quad-point,etc], _delete_func>>
  // Stores functions for deallocating void* stored data (i.e. for stateful/old values).
  std::map<unsigned int, std::function<void(void*)>> _delete_funcs;

  // True to run error checking.
  bool _errcheck;
  // list<map<value_id, true>>. In sequences of values depending on other values, this tracks what
  // values have been used between dependency chains - enabling cyclical value dependency
  // detection. oldValue retrieval breaks dependency chains.
  std::list<std::map<unsigned int, bool>> _cycle_stack;
};

