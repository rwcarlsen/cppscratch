#pragma once

#include <map>
#include <vector>
#include <list>
#include <string>
#include <functional>
#include <typeinfo>

typedef unsigned int ValId;
typedef unsigned int BlockId;

class Location;

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
// to/from string streams for arbitrary types.  For Value class users that store a generic Value
// ptr/ref, the virtual load/store functions dispatch down to the subclass Valuer<T>
// (type-specific) subclasses that can then call their type-specific load/store functions.  This
// trick is also used to be able to clone and destruct objects of arbitrary type without casting
// them or fancy lambda hacks.
class Value
{
public:
  virtual void store(std::ostream & s) {}
  virtual void load(std::istream & s) {}
  virtual Value * clone() { return new Value(*this); }
  virtual ~Value() {}

  // For contexts where values are used in map keys directly/indirectly or are compared, this
  // function is used.
  virtual bool lessThan(const Value &) { return false; }
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
class ValuerBase
{
public:
  virtual ~ValuerBase() {}
  // Returns a unique int identifying the C++ type of the stored value.  Used for error checking
  // for compatible types in the ValueStore.
  virtual size_t type() = 0;
  // Returns the (nick)name for the C++ type of the stored value.  Used for error checking
  // for compatible types in the ValueStore.
  virtual std::string type_name() = 0;
  // This function is a hook that is called whenenver a ValueStore this valuer is registered with
  // performs a "shift" operation (i.e. a simulation step or moving current values to old, etc.).
  virtual void shift() {}
};

// Valuer is a base class for all mesh-location-specific values/properties managed/stored by the
// ValueStore.  Creating custom properties/values is as simple as inheriting from the Valuer class
// and overriding the 'get' function to compute and return the desired value.  Valuer objects
// should be added to a ValueStore where they can be retrieved and otherwise managed in helpful
// ways.
template <typename T>
class Valuer : public ValuerBase
{
public:
  virtual ~Valuer() {}
  // Computes and return a value at the specified mesh location.  get should be idempotent - i.e.
  // consecutive calls to get (with no significant simulation state changes between them) should
  // result in the same answer.
  virtual T get(const Location &) = 0;
  // Optionally return an initial condition "old" value.  This is used if an old (i.e. previous
  // simulation time step) version of this value is requested, but there was no prior current
  // value to become the old value.
  virtual T initialOld(const Location &) { return T{}; };
  // Optionally return an initial condition "older" value.  This is used if an older (i.e.
  // previous previous simulation time step) version of this value is requested, but there was no
  // prior old value to become the older value.
  virtual T initialOlder(const Location &) { return T{}; };
  virtual size_t type() final override { return typeid(T).hash_code(); }
  virtual std::string type_name() final override { return typeid(T).name(); }
};

class Location
{
public:
  Location(unsigned int nqp,
           unsigned int qp,
           unsigned int elem = 1,
           BlockId block_id = 0,
           unsigned int face_id = 0)
    : nqp(nqp), qp(qp), elem_id(elem), block_id(block_id), face_id(face_id), custom(nullptr)
  {
  }

  Location(const Location & loc)
  {
    elem_id = loc.elem_id;
    face_id = loc.face_id;
    block_id = loc.block_id;
    qp = loc.qp;
    nqp = loc.nqp;
    custom.reset(loc.custom ? loc.custom->clone() : nullptr);
  }

  friend bool operator!=(const Location & lhs, const Location & rhs)
  {
    return lhs.elem_id != rhs.elem_id || lhs.face_id != rhs.face_id || lhs.qp != rhs.qp;
  }

  unsigned int elem_id;
  unsigned int face_id;
  BlockId block_id;
  unsigned int qp;
  unsigned int nqp;
  // Allows users to include arbitrary extra info in custom key sorting in value-store.
  std::unique_ptr<Value> custom;
};

// A Custom comparator for creating a ValueStore that stores values on the mesh by a combined
// element id, face id, and quadrature point index key.
class QpKey
{
public:
  bool operator()(const Location & lhs, const Location & rhs) const
  {
    return (lhs.elem_id != rhs.elem_id && lhs.elem_id < rhs.elem_id) ||
           (lhs.face_id != rhs.face_id && lhs.face_id < rhs.face_id) ||
           (lhs.qp != rhs.qp && lhs.qp < rhs.qp) ||
           (lhs.custom && rhs.custom && lhs.custom->lessThan(*rhs.custom.get())) ||
           (lhs.custom != nullptr && rhs.custom == nullptr);
  }
};

// Manages the calculation of named+id'd values by mesh location and optionally handles storage
// and retrieval of stateful/prior versions of those values.  Sets of named values (e.g.
// simulation-global) can be registered with a particular ValueStore.  When values are retrieved
// by name or id, cyclical dependency detection is performed (values can depend on each-other i.e.
// a registered Valuer's get function can retrieve a value from the same ValueStore),
// type-consistency checking is performed, and requests for old/older versions of particular
// values are tracked triggering their storage automatically.
//
// Values can be tracked/stored at custom locations on the mesh by writing a custom Comparator
// that defines a unique ordering for Location objects (e.g. see QpKey comparator for an example).
//
// Unless otherwise noted, an 'id' argument to a function refers to the unique id assigned to that
// added/registered value - i.e. the id returned by the add(...), addMapper(...), and id(...)
// functions.
template <typename Cmp>
class ValueStore
{
public:
  ValueStore(bool errcheck = false)
    : _errcheck(errcheck), _cycle_stack(1, std::map<ValId, bool>{}){};

  ~ValueStore()
  {
    for (int i = 0; i < _valuers.size(); i++)
      if (_own_valuer[i])
        delete _valuers[i];
  }

  // Returns the id of the named, *previously* added/registered value or mapper.  Throws an error
  // if name has never been added/registered.
  inline ValId id(const std::string & name)
  {
    std::map<std::string, ValId>::iterator it = _ids.find(name);
    if (it == _ids.end())
      throw std::runtime_error("value " + name + " doesn't exist (yet?)");
    return it->second;
  }

  // Explicitly marks the named value (which must have already been added) for
  // tracking of its stateful old values.
  inline void wantOld(const std::string & name) { _want_old[id(name)] = true; }
  // Explicitly marks the named value (which must have already been added) for
  // tracking of its stateful older values.
  inline void wantOlder(const std::string & name) { _want_older[id(name)] = true; }

  // addMapper allows the given value name to actually compute+return the value from another
  // valuer determined by calling the passed mapper function.  It returns a unique, persistent id
  // assigned to the added value/mapper.  When get<...>("myval", location)
  // is called, if "myval" was registered via addMapper, then its corresponding mapper
  // function would be called (passing in the location) and the returned value id would be used to
  // compute+fetch the actual value.  It is a mechanism to allow one value/id to be a conditional
  // alias mapping to arbitrary other value id's depending on location and any other desired state
  // closed over by the mapper function.
  ValId addMapper(const std::string & name, std::function<ValId(const Location &)> mapper)
  {
    return add(name, nullptr, mapper, false);
  }

  // Turn error checking on/off (true to enable).
  void errcheck(bool check) { _errcheck = check; }

  // Registers a valuer with the given name to be tracked by the ValueStore. If take_ownership is
  // true, the ValueStore will manage the memory of q and handle its deallocation. It returns a
  // unique, persistent id assigned to the added/registered value.  Calls to the ValueStore's get
  // using the passed name will trigger calls to q's get function.
  template <typename T>
  ValId add(const std::string & name, Valuer<T> * q, bool take_ownership = false)
  {
    return add(name, q, {}, take_ownership);
  }

  // Computes and returns the current value for the given value id at the specified mesh location.
  template <typename T>
  T get(ValId id, const Location & loc)
  {
    if (_have_mapper[id])
      return get<T>(_mapper[id](loc), loc);

    if (_errcheck)
    {
      if (_cycle_stack.back().count(id) > 0)
      {
        std::string items;
        for (auto & item : _cycle_stack.back())
          items += "', '" + _names[item.first];
        throw std::runtime_error("cyclical value dependency detected (reuse of '" + _names[id] +
                                 "') involving " + items.substr(3) + "'");
      }
      _cycle_stack.back()[id] = true;
      checkType<T>(id);
    }

    auto val = static_cast<Valuer<T> *>(_valuers[id])->get(loc);
    _external_curr[id] = true;

    // mark this property as computed if we need its old value and stage/store value
    if (_want_old[id] || _want_older[id])
      stageOld(id, loc, val);
    if (_errcheck)
      _cycle_stack.back().erase(id);
    return val;
  }

  // Alias for get(id(name), loc)
  template <typename T>
  inline double get(const std::string & name, const Location & loc)
  {
    return get<T>(id(name), loc);
  }

  // Computes and returns the previous value for the given value id at the specified mesh
  // location.  "Previous" refers to the value "get(...)" returned prior to the most recent call
  // to the ValueStore's shift() function (i.e. the value on the previous time step).
  template <typename T>
  T getOld(ValId id, const Location & loc)
  {
    return getStored<T>(_old_vals, _want_old, id, loc);
  }

  // Alias for getOld(id(name), loc)
  template <typename T>
  T getOld(const std::string & name, const Location & loc)
  {
    return getOld<T>(id(name), loc);
  }

  // Computes and returns the previous previous value for the given value id at the specified mesh
  // location.  "Previous previous" refers to the value "get(...)" returned prior to the call
  // before the most recent call to the ValueStore's shift() function (i.e. the value on the
  // previous previous time step).
  template <typename T>
  T getOlder(ValId id, const Location & loc)
  {
    return getStored<T>(_older_vals, _want_older, id, loc, true);
  }

  // Alias to getOlder(id(name), loc)
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
    for (ValId id = 0; id < _valuers.size(); id++)
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
  // Notifies all added/registered Valuers of the shift by calling their shift functions.
  void shift()
  {
    _older_vals.swap(_old_vals);
    _old_vals.swap(_curr_vals);
    for (auto valuer : _valuers)
      valuer->shift();
  }

private:
  // handles all addition of new values to this store.  _ids, _valuers, etc. structures themselves
  // (not
  // necsesarily the items they hold) should generally NOT be modified by anything other than this
  // function.
  ValId add(const std::string & name,
            ValuerBase * q,
            std::function<ValId(const Location &)> mapper,
            bool take_ownership)
  {
    ValId id = _valuers.size();
    _ids[name] = id;
    _names.push_back(name);
    _valuers.push_back(q);
    _want_old.push_back(false);
    _want_older.push_back(false);
    _own_valuer.push_back(take_ownership);
    _mapper.push_back(mapper);
    _have_mapper.push_back(q == nullptr);
    _external_curr.push_back(false);
    return id;
  }

  template <typename T>
  T getStored(std::map<ValId, std::map<Location, Value *, Cmp>> & vals,
              std::vector<bool> & want,
              ValId id,
              const Location & loc,
              bool older = false)
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

    // There was no previous old value, so we use its initial value.  We also need to
    // stage/store if there is no corresponding stored current value to become the next old value.
    T val = static_cast<Valuer<T> *>(_valuers[id])->initialOld(loc);
    if (older)
      val = static_cast<Valuer<T> *>(_valuers[id])->initialOlder(loc);
    return val;
  }

  // Stores/saves a computed value so it can be used as old on the next iteration/step (i.e. after
  // shift call).
  template <typename T>
  void stageOld(ValId id, const Location & loc, const T & val)
  {
    auto prev = _curr_vals[id][loc];
    if (prev != nullptr)
      delete prev;
    _curr_vals[id][loc] = new TypedValue<T>(val);
  }

  // Used to ensure the c++ type of a value being retrieved (i.e. T) is the same as the c++ type
  // that was stored/added there.
  template <typename T>
  inline void checkType(ValId id)
  {
    auto valuer = _valuers[id];
    if (valuer && typeid(T).hash_code() != valuer->type())
      throw std::runtime_error("wrong type requested: " + valuer->type_name() + " != " +
                               typeid(T).name());
  }

  // map<value_name, value_id>
  std::map<std::string, ValId> _ids;
  // map<value_id, value_name>
  std::vector<std::string> _names;

  // map<value_id, valuer>
  std::vector<ValuerBase *> _valuers;
  // true if we own the memory of the valuer
  std::vector<bool> _own_valuer;
  // map<value_id, want_old>. True if an old version of the value has (ever) been requested.
  std::vector<bool> _want_old;
  // map<value_id, want_older>. True if an older version of the value has (ever) been requested.
  std::vector<bool> _want_older;
  // map<value_id, valuer>
  std::vector<bool> _have_mapper;
  // map<value_id, mapper>
  std::vector<std::function<ValId(const Location &)>> _mapper;

  // map<value_id, map<[elem_id,face_id,quad-point,etc], val>>>.
  // Caches any computed/retrieved values for which old values are needed.
  std::map<ValId, std::map<Location, Value *, Cmp>> _curr_vals;
  // Stores needed/requested old values.
  std::map<ValId, std::map<Location, Value *, Cmp>> _old_vals;
  // Stores needed/requested older values.
  std::map<ValId, std::map<Location, Value *, Cmp>> _older_vals;

  // map<value_id, external_curr>>
  // Stores whether or not the get<...>(...) function is ever called externally (from outside the
  // ValueStore class).  If this is never marked true, then getOld needs to invoke evaluation of the
  // current values on its own.
  std::vector<bool> _external_curr;

  // True to run error checking.
  bool _errcheck;
  // list<map<value_id, true>>. In sequences of values depending on other values, this tracks what
  // values have been used in dependency chains - enabling cyclical value dependency
  // detection. getOld[er] retrieval breaks dependency chains.
  std::list<std::map<ValId, bool>> _cycle_stack;
};

typedef ValueStore<QpKey> QpStore;

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
