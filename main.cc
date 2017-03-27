
#include <iostream>
#include <map>
#include <vector>

class Point
{
public:
  double x;
  double y;
  double z;
};

typedef unsigned int Node;

class Element
{
public:
  Element(unsigned int id) : _id(id), _parent_id(0) {}
  Element(unsigned int id, unsigned int parent_id) : _id(id), _parent_id(parent_id) {}
  inline unsigned int unique_id() { return _id; }
  inline Element parent() const { return Element(_parent_id); }

private:
  unsigned int _id;
  unsigned int _parent_id;
};

class FEProblem;

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
  Point point() const { return {1, 2, 5}; }
  inline Element elem() const { return Element(_elem_id, _elem_parent_id); }
  unsigned int elem_id() const {return _elem_id;}
  FEProblem & fep() const { return _fep; }
  inline Location parent_loc() const
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

template <typename T>
class QpValuer
{
public:
  virtual T value(const Location &) = 0;
};

class QpStore
{
public:
  QpStore(bool errcheck = false) : _errcheck(errcheck){};

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
      if (_cycle_stack.count(id) > 0)
        throw std::runtime_error("cyclical value dependency detected");
      _cycle_stack[id] = true;
      checkType<T>(id);
    }

    // mark this property as computed if we need its old value
    if (_want_old[id])
      _external_curr[id][loc] = true;

    auto val = static_cast<QpValuer<T> *>(_valuers[id])->value(loc);
    if (_want_old[id])
      stageOldVal(id, loc, val);
    if (_errcheck)
      _cycle_stack.erase(id);
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
      _cycle_stack.clear();
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
      // true if value is called by someone else.
      _external_curr[id][loc] = false;
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
  // map<value_id, true>. In sequences of values depending on other values, this trackes what
  // values have been used enabling cyclical value dependency detection.
  std::map<unsigned int, bool> _cycle_stack;
};

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

template <typename T>
class MeshStore
{
public:
  void storeProp(const Location & loc, const std::string & prop)
  {
    resize(loc)[loc.qp()] = loc.fep().getProp<T>(prop, loc);
  }

  void store(const Location & loc, MeshStore<T> & other)
  {
    resize(loc)[loc.qp()] = other.retrieve(loc);
  }

  void store(const Location & loc, T val) { resize(loc)[loc.qp()] = val; }

  T retrieve(const Location & loc) { return resize(loc)[loc.qp()]; }

  std::vector<T> & resize(const Location & loc)
  {
    auto & vec = _data[loc.elem_id()];
    if (vec.size() <= loc.qp())
      vec.resize(loc.qp() + 1);
    return vec;
  }

private:
  std::map<Element *, std::vector<T>> _data;
};

class ConstQpValuer : public QpValuer<double>
{
public:
  ConstQpValuer(double val) : _val(val) {}
  virtual double value(const Location & loc) override { return _val; }

private:
  double _val;
};

class IncrementQpValuer : public QpValuer<double>
{
public:
  virtual double value(const Location & loc) override { return _next++; }

private:
  int _next = 0;
};

class DepQpValuer : public QpValuer<double>
{
public:
  DepQpValuer(double toadd, const std::string & dep) : _toadd(toadd), _dep(dep) {}
  virtual double value(const Location & loc) override
  {
    return loc.fep().getProp<double>(_dep, loc) + _toadd;
  }

private:
  double _toadd;
  std::string _dep;
};

class MyMat
{
public:
  MyMat(FEProblem & fep, std::string name, std::vector<std::string> props)
  {
    for (auto & prop : props)
    {
      _vars.push_back(new ConstQpValuer(_vars.size() + 42000));
      fep.registerProp(_vars.back(), name + "-" + prop);
    }
  }

private:
  std::vector<QpValuer<double> *> _vars;
};

void
scalingStudy()
{
  unsigned int props_per_mat = 10;
  unsigned int n_mats = 10;
  unsigned int n_steps = 10;
  unsigned int n_quad_points = 1000000;
  unsigned int n_repeat_calcs = 5;

  FEProblem fep;

  std::vector<std::string> prop_names;
  for (int i = 0; i < props_per_mat; i++)
    prop_names.push_back("prop" + std::to_string(i + 1));

  for (int i = 0; i < n_mats; i++)
    new MyMat(fep, "mat" + std::to_string(i + 1), prop_names);

  std::vector<unsigned int> prop_ids;
  for (auto & prop : prop_names)
    for (int i = 0; i < n_mats; i++)
      prop_ids.push_back(fep.prop_id("mat" + std::to_string(i + 1) + "-" + prop));

  for (int t = 0; t < n_steps; t++)
  {
    std::cout << "step " << t + 1 << std::endl;
    for (int rep = 0; rep < n_repeat_calcs; rep++)
    {
      for (int i = 0; i < n_quad_points; i++)
      {
        for (auto & prop : prop_ids)
          fep.getProp<double>(prop, Location(fep, n_quad_points, i));
      }
    }
  }
};

void
basicPrintoutTest()
{
  FEProblem fep;
  MyMat mat(fep, "mymat", {"prop1", "prop7"});

  std::cout << "mymat-prop1=" << fep.getProp<double>("mymat-prop1", Location(fep, 3, 1))
            << std::endl;
  std::cout << "mymat-prop1=" << fep.getProp<double>("mymat-prop1", Location(fep, 3, 2))
            << std::endl;
  std::cout << "mymat-prop7=" << fep.getProp<double>("mymat-prop7", Location(fep, 3, 2))
            << std::endl;

  IncrementQpValuer iq;
  auto id = fep.registerProp(&iq, "inc-qp");

  std::cout << "inc-qp=" << fep.getProp<double>(id, Location(fep, 1, 0)) << std::endl;
  std::cout << "  old inc-qp=" << fep.getPropOld<double>(id, Location(fep, 1, 0)) << std::endl;
  std::cout << "--- shift\n";
  fep.shift();
  std::cout << "inc-qp=" << fep.getProp<double>(id, Location(fep, 1, 0)) << std::endl;
  std::cout << "  old inc-qp=" << fep.getPropOld<double>(id, Location(fep, 1, 0)) << std::endl;
  std::cout << "--- shift\n";
  fep.shift();
  std::cout << "inc-qp=" << fep.getProp<double>(id, Location(fep, 1, 0)) << std::endl;
  std::cout << "  old inc-qp=" << fep.getPropOld<double>(id, Location(fep, 1, 0)) << std::endl;
  std::cout << "inc-qp=" << fep.getProp<double>(id, Location(fep, 1, 0)) << std::endl;
  std::cout << "  old inc-qp=" << fep.getPropOld<double>(id, Location(fep, 1, 0)) << std::endl;
  std::cout << "--- shift\n";
  fep.shift();
  std::cout << "inc-qp=" << fep.getProp<double>(id, Location(fep, 1, 0)) << std::endl;
  std::cout << "  old inc-qp=" << fep.getPropOld<double>(id, Location(fep, 1, 0)) << std::endl;
}

void
wrongTypeTest()
{
  FEProblem fep(true);
  MyMat mat(fep, "mymat", {"prop1", "prop7"});
  // throw error - wrong type.
  try
  {
    fep.getProp<int>("mymat-prop1", Location(fep, 0, 1));
  }
  catch (std::runtime_error err)
  {
    std::cout << err.what() << std::endl;
    return;
  }
  std::cout << "wrongTypeTest FAIL\n";
}

void
cyclicalDepTest()
{
  FEProblem fep(true);
  DepQpValuer dq1(1, "dep2");
  DepQpValuer dq2(1, "dep3");
  DepQpValuer dq3(1, "dep1");
  auto id1 = fep.registerProp(&dq1, "dep1");
  auto id2 = fep.registerProp(&dq2, "dep2");
  auto id3 = fep.registerProp(&dq3, "dep3");

  // throw error - cyclical dependency
  try
  {
    fep.getProp<double>(id1, Location(fep, 0, 1));
  }
  catch (std::runtime_error err)
  {
    std::cout << err.what() << std::endl;
    return;
  }
  std::cout << "cyclicalDepTest FAIL\n";
}

int
main(int argc, char ** argv)
{
  scalingStudy();
  basicPrintoutTest();
  wrongTypeTest();
  cyclicalDepTest();

  // FEProblem fep;
  // MyMat mat(fep, "mymat", {"prop1", "prop7"});
  // MyDepOldMat matdepold(fep, "mymatdepold", "mymat-prop7");

  // std::cout << fep.getProp<double>("mymat-prop1", Location(fep, 3, 1)) <<
  // std::endl;
  // std::cout << fep.getProp<double>("mymat-prop1", Location(fep, 3, 2)) <<
  // std::endl;
  // std::cout << fep.getProp<double>("mymat-prop7", Location(fep, 3, 2)) <<
  // std::endl;

  // std::cout << "printing older props:\n";
  // Location loc(fep, 1);
  // for (int i = 0; i < 8; i++)
  //{
  //  std::cout << "\nprop7=" << fep.getProp<double>("mymat-prop7", loc) <<
  //  std::endl;
  //  std::cout << "    olderprop=" << fep.getProp<double>("mymatdepold", loc)
  //  << std::endl;
  //}

  return 0;
}
