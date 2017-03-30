#pragma once

#include <map>
#include <vector>
#include <list>
#include <string>

class Location;
class QpStore;

// These are stubs for MOOSE data store/load functions.
template <typename T>
void
dataLoad(std::istream & s, T val)
{
}
template <typename T>
void
dataStore(std::ostream & s, T val)
{
}

// This class and the TypedValue class are necessary to perform a "trick" to enable serialization
// to/from string streams for arbitrary types.  The virtual load/store functions dispatch down to
// the subclass QpValuer<T> type-specific subclasses that can then call their type-specific
// load/store functions.  This trick is also used to be able to clone and destruct objects of
// arbitrary type without casting them or fancy lambda hacks.
class Value
{
public:
  virtual void store(std::ostream & s) = 0;
  virtual void load(std::istream & s) = 0;
  virtual Value * clone() = 0;
  virtual ~Value() {}
};

template <typename T>
class TypedValue final : public Value
{
public:
  TypedValue(T val) : val(val) {}
  virtual ~TypedValue() {}
  virtual void store(std::ostream & s) override { dataStore(s, val); }
  virtual void load(std::istream & s) override { dataLoad(s, val); }
  virtual Value * clone() override { return new TypedValue<T>(val); }
  T val;
};

// This class exists to accomplish the same "trick" as described in the Value class doc comment.
class QpValuerBase
{
public:
  virtual ~QpValuerBase() {}
  virtual size_t type() = 0;
  virtual std::string type_name() = 0;
};

template <typename T>
class QpValuer : public QpValuerBase
{
public:
  virtual ~QpValuer() {}
  virtual T get(const Location &) = 0;
  virtual T initialOld(const Location &) { return T{}; };
  virtual size_t type() final override { return typeid(T).hash_code(); }
  virtual std::string type_name() final override { return typeid(T).name(); }
};

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
    _func(loc);
    return *_var;
  }

private:
  std::function<void(const Location &)> _func;
  T * _var;
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

class Location
{
public:
  Location(QpStore * store,
           unsigned int nqp,
           unsigned int qp,
           unsigned int elem = 1,
           unsigned int parent_id = 0,
           unsigned int block_id = 0,
           unsigned int face_id = 0)
    : nqp(nqp),
      qp(qp),
      _store(store),
      elem_parent_id(parent_id),
      elem_id(elem),
      block_id(block_id),
      face_id(face_id)
  {
  }
  QpStore & vals() const { return *_store; }
  inline Location parent() const { return Location(_store, nqp, qp, elem_id, elem_parent_id); }

  friend bool operator<(const Location & lhs, const Location & rhs)
  {
    return lhs.elem_id < rhs.elem_id || lhs.face_id < rhs.face_id || lhs.qp < rhs.qp;
  }

  unsigned int elem_id;
  unsigned int elem_parent_id;
  unsigned int face_id;
  unsigned int block_id;
  unsigned int qp;
  unsigned int nqp;

private:
  QpStore * _store;
};

// Unless otherwise noted, an 'id argument to a function refers to the unique id assigned to that
// added/registered value - i.e. the id returned by the add(...), addMapper(...), and id(...)
// functions.
class QpStore
{
public:
  QpStore(bool errcheck = false) : _errcheck(errcheck), _cycle_stack(1, {}){};

  ~QpStore()
  {
    for (int i = 0; i < _valuers.size(); i++)
      if (_own_valuer[i])
        delete _valuers[i];
  }

  // Returns the id of the named, *previously* added/registered value or mapper.  Throws an error
  // if name has never been added/registered.
  inline unsigned int id(const std::string & name)
  {
    if (_ids.count(name) == 0)
      throw std::runtime_error("value " + name + " doesn't exist (yet?)");
    return _ids[name];
  }

  inline void wantOld(const std::string & name) { _want_old[id(name)] = true; }
  inline void wantOlder(const std::string & name) { _want_older[id(name)] = true; }

  // addMapper allows the given value name to actually compute+return the value from another
  // valuer determined by calling the passed mapper function.  It returns a unique, persistent id
  // assigned to the added value/mapper.  When get<...>("myval", location)
  // is called, if "myval" was registered via addMapper, then its corresponding mapper
  // function would be called (passing in the location) and the returned value id would be used to
  // compute+fetch the actual value.  It is a mechanism to allow one value/id to be a conditional
  // alias mapping to arbitrary other value id's depending on location and any other desired state
  // closed over by the mapper function.
  unsigned int addMapper(const std::string & name,
                         std::function<unsigned int(const Location &)> mapper)
  {
    return add(name, nullptr, mapper, false);
  }

  void errcheck(bool check) { _errcheck = check; }

  // It returns a unique, persistent id assigned to the added/registered value.
  template <typename T>
  unsigned int add(QpValuer<T> * q, const std::string & name, bool take_ownership = false)
  {
    return add(name, q, {}, take_ownership);
  }

  template <typename T>
  T get(unsigned int id, const Location & loc)
  {
    if (_have_mapper[id])
      return get<T>(_mapper[id](loc), loc);

    if (_errcheck)
    {
      if (_cycle_stack.back().count(id) > 0)
        throw std::runtime_error("cyclical value dependency detected");
      _cycle_stack.back()[id] = true;
      checkType<T>(id);
    }

    auto val = static_cast<QpValuer<T> *>(_valuers[id])->get(loc);
    if (_want_old[id] || _want_older[id])
    {
      // mark this property as computed if we need its old value and stage/store value
      _external_curr[id] = true;
      stageOld(id, loc, val);
    }
    if (_errcheck)
      _cycle_stack.back().erase(id);
    return val;
  }

  template <typename T>
  inline double get(const std::string & name, const Location & loc)
  {
    return get<T>(id(name), loc);
  }

  template <typename T>
  T getOld(unsigned int id, const Location & loc)
  {
    return getStored<T>(_old_vals, _want_old, id, loc);
  }

  template <typename T>
  T getOld(const std::string & name, const Location & loc)
  {
    return getOld<T>(id(name), loc);
  }

  template <typename T>
  T getOlder(unsigned int id, const Location & loc)
  {
    return getStored<T>(_older_vals, _want_older, id, loc);
  }

  template <typename T>
  T getOlder(const std::string & name, const Location & loc)
  {
    return getOlder<T>(id(name), loc);
  }

  // Projects/copies computed old values at the source locations to live under destination
  // locations (mapped one to one in the ordered vectors).  When doing e.g. mesh adaptivity, call
  // this to project values at old locations (srcs) to new locations (dsts) where they were never
  // explicitly computed before.  This needs to be called *after* the call to shift and *before*
  // calls to getOld.
  void project(std::vector<const Location *> srcs, std::vector<const Location *> dsts)
  {
    for (unsigned int id = 0; id < _valuers.size(); id++)
    {
      for (int i = 0; i < srcs.size(); i++)
      {
        auto & src = *srcs[i];
        auto & dst = *dsts[i];
        delete _old_vals[id][dst];
        _old_vals[id][dst] = _old_vals[id][src]->clone();
      }
      for (auto src : srcs)
        delete _old_vals[id][*src];
    }
  }

  // Moves stored "current" values to "older" values, discarding any previous "older" values.
  void shift()
  {
    _older_vals.swap(_old_vals);
    _old_vals.swap(_curr_vals);
  }

private:
  // handles all addition of new values to this store.  _ids, _valuers, etc. structures themselves
  // (not
  // necsesarily the items they hold) should generally NOT be modified by anything other than this
  // function.
  unsigned int add(const std::string & name,
                   QpValuerBase * q,
                   std::function<unsigned int(const Location &)> mapper,
                   bool take_ownership)
  {
    unsigned int id = _valuers.size();
    _ids[name] = id;
    _valuers.push_back(q);
    _want_old.push_back(false);
    _want_older.push_back(false);
    _own_valuer.push_back(take_ownership);
    _mapper.push_back(mapper);
    _have_mapper.push_back(q != nullptr);
    return id;
  }

  template <typename T>
  T getStored(std::map<unsigned int, std::map<Location, Value *>> & vals,
              std::vector<bool> & want,
              unsigned int id,
              const Location & loc)
  {
    if (_have_mapper[id])
      return getStored<T>(vals, want, _mapper[id](loc), loc);

    if (_errcheck)
    { // make sure there are no returns between this code and the next "if(_errcheck)"
      _cycle_stack.push_back({});
      checkType<T>(id);
    }

    if (!want[id])
      want[id] = true;

    // force computation of current value in preparation for next old value if there was no other
    // explicit calls to value for this property/location combo.
    if (!_external_curr[id])
    {
      get<T>(id, loc);
      // reset to false because above get<>(...) call sets it to true, but we only want it to be
      // true if value is called by someone else (i.e. externally).
      _external_curr[id] = false;
    }

    if (_errcheck)
      _cycle_stack.pop_back();

    if (vals[id].count(loc) > 0)
      return static_cast<TypedValue<T> *>(vals[id][loc])->val;

    // There was no previous old value, so we use the zero/default value.  We also need to
    // stage/store if there is no corresponding stored current value to become the next old value.
    T val = static_cast<QpValuer<T> *>(_valuers[id])->initialOld(loc);
    stageOld(id, loc, val);
    return val;
  }

  // Stores/saves a computed value so it can be used as old next iteration/step (i.e. after shift
  // call).
  template <typename T>
  void stageOld(unsigned int id, const Location & loc, const T & val)
  {
    auto prev = _curr_vals[id][loc];
    if (prev != nullptr)
      delete prev;
    _curr_vals[id][loc] = new TypedValue<T>(val);
  }

  // Used to ensure the c++ type of a value being retrieved (i.e. T) is the same as the c++ type
  // that was stored/added there.
  template <typename T>
  inline void checkType(unsigned int id)
  {
    auto valuer = _valuers[id];
    if (valuer && typeid(T).hash_code() != valuer->type())
      throw std::runtime_error("wrong type requested: " + valuer->type_name() + " != " +
                               typeid(T).name());
  }

  // map<value_name, value_id>
  std::map<std::string, unsigned int> _ids;

  // the following contiguous block of member variables are all vectors where the index represents
  // the value id - i.e. map<value_id, [something]>

  // map<value_id, valuer>
  std::vector<QpValuerBase *> _valuers;
  // true if we own the memory of the valuer
  std::vector<bool> _own_valuer;
  // map<value_id, want_old>. True if an old version of the value has (ever) been requested.
  std::vector<bool> _want_old;
  // map<value_id, want_older>. True if an older version of the value has (ever) been requested.
  std::vector<bool> _want_older;
  std::vector<bool> _have_mapper;
  std::vector<std::function<unsigned int(const Location &)>> _mapper;

  // map<value_id, map<[elem_id,face_id,quad-point,etc], val>>>.
  // Caches any computed/retrieved values for which old values are needed.
  std::map<unsigned int, std::map<Location, Value *>> _curr_vals;
  // Stores needed/requested old values.
  std::map<unsigned int, std::map<Location, Value *>> _old_vals;
  // Stores needed/requested older values.
  std::map<unsigned int, std::map<Location, Value *>> _older_vals;

  // map<value_id, external_curr>>
  // Stores whether or not the get<...>(...) function is ever called externally (from outside the
  // QpStore
  // class).  If this is never marked true, then getOld needs to invoke evaluation of the
  // current values on its own.
  std::map<unsigned int, bool> _external_curr;

  // True to run error checking.
  bool _errcheck;
  // list<map<value_id, true>>. In sequences of values depending on other values, this tracks what
  // values have been used in dependency chains - enabling cyclical value dependency
  // detection. getOld[er] retrieval breaks dependency chains.
  std::list<std::map<unsigned int, bool>> _cycle_stack;
};

////////////////////////
// specialization for storing/loading Location objects and arbitrary typed values stored in Value
// objects
//

template <>
inline void
dataStore(std::ostream & stream, const Location & loc)
{
  dataStore(stream, loc.elem_id);
  dataStore(stream, loc.face_id);
  dataStore(stream, loc.qp);
}

template <>
inline void
dataLoad(std::istream & stream, Location & loc)
{
  dataLoad(stream, &loc.elem_id);
  dataLoad(stream, &loc.face_id);
  dataLoad(stream, &loc.qp);
}

template <>
inline void
dataStore(std::ostream & stream, Value * v)
{
  v->store(stream);
}

template <>
inline void
dataLoad(std::istream & stream, Value * v)
{
  v->load(stream);
}
