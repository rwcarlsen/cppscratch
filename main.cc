
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

typedef unsigned int Elem;
typedef unsigned int Node;

class FEProblem;

class Location
{
public:
  Location(FEProblem& fep, unsigned int nqp, unsigned int qp) : _nqp(nqp), _qp(qp), _fep(fep) { }
  unsigned int qp() const {return _qp;}
  unsigned int nqp() const {return _nqp;}
  Point point() const {return {1, 2, 5};}
  Elem* elem() const {return nullptr;}
  Node* node() const {return nullptr;}
  FEProblem& fep() const {return _fep;}
private:
  unsigned int _nqp;
  unsigned int _qp;
  FEProblem& _fep;
};

template <typename T>
class QpValuer {
public:
  virtual T value(const Location &) = 0;
};

class QpStore
{
public:
  inline unsigned int id(const std::string& name)
  {
    if (_ids.count(name) == 0)
      throw std::runtime_error("material property " + name + " doesn't exist");
    return _ids[name];
  }

  template <typename T>
  unsigned int registerValue(QpValuer<T>* q, const std::string& name) {
    unsigned int id = _valuers.size();
    _ids[name] = id;
    _valuers.push_back(q);
    _want_old.push_back(false);
    return id;
  }

  template <typename T>
  T value(unsigned int id, const Location& loc)
  {
    auto val = static_cast<QpValuer<T>*>(_valuers[id])->value(loc);
    stageOldVal(id, loc, val);
    return val;
  }

  template <typename T>
  inline double value(const std::string& name, const Location& loc)
  {
    return value<T>(id(name), loc);
  }

  template <typename T>
  T oldValue(unsigned int id, const Location& loc)
  {
    if (_old_vals[id][loc.elem()].size() >= loc.qp()+1)
      return *static_cast<T*>(_old_vals[id][loc.elem()][loc.qp()]);

    _want_old[id] = true;
    T val{};
    stageOldVal(id, loc, val);
    return val;
  }

  template <typename T>
  T oldValue(const std::string& name, const Location& loc)
  {
    return oldValue<T>(id(name), loc);
  }

  void shift() { _old_vals.swap(_curr_vals); }

private:
  template <typename T>
  void stageOldVal(unsigned int id, const Location & loc, const T & val)
  {
    if (!_want_old[id])
      return;
    auto & vec = _curr_vals[id][loc.elem()];
    vec.resize(loc.nqp(), nullptr);
    if (vec[loc.qp()] != nullptr)
      delete static_cast<T*>(vec[loc.qp()]);
    vec[loc.qp()] = new T(val);
  }

  std::map<std::string, unsigned int> _ids;
  std::vector<void*> _valuers;
  std::vector<bool> _want_old;
  // map<value_id, map<elem, map<quad-point, val>>>
  std::map<unsigned int, std::map<Elem*, std::vector<void*>>> _curr_vals;
  std::map<unsigned int, std::map<Elem*, std::vector<void*>>> _old_vals;
};

class FEProblem
{
public:
  template <typename T>
  inline unsigned int registerProp(QpValuer<T>* v, const std::string& prop) { return _propstore.registerValue<T>(v, prop); }

  template <typename T>
  inline T getProp(const std::string& name, const Location& loc) {return _propstore.value<T>(name, loc);}
  template <typename T>
  inline T getProp(unsigned int prop, const Location& loc) {return _propstore.value<T>(prop, loc);}

  template <typename T>
  inline T getPropOld(const std::string& name, const Location& loc) {return _propstore.oldValue<T>(name, loc);}
  template <typename T>
  inline T getPropOld(unsigned int prop, const Location& loc) {return _propstore.oldValue<T>(prop, loc);}

  inline unsigned int prop_id(const std::string& name) { return _propstore.id(name); }

  void shift() {_propstore.shift();}

private:
  QpStore _propstore;
};

template <typename T>
class MeshStore
{
public:
  void storeProp(const Location& loc, const std::string& prop)
  {
    resize(loc)[loc.qp()] = loc.fep().getProp<T>(prop, loc);
  }

  void store(const Location& loc, MeshStore<T>& other)
  {
    resize(loc)[loc.qp()] = other.retrieve(loc);
  }

  void store(const Location& loc, T val)
  {
    resize(loc)[loc.qp()] = val;
  }

  T retrieve(const Location& loc) {
    return resize(loc)[loc.qp()];
  }

  std::vector<T>& resize(const Location& loc)
  {
    auto& vec = _data[loc.elem()];
    if (vec.size() <= loc.qp())
      vec.resize(loc.qp() + 1);
    return vec;
  }

private:
  std::map<Elem*, std::vector<T>> _data;
};

class ConstQpValuer : public QpValuer<double>
{
public:
  ConstQpValuer(double val) : _val(val) { }
  virtual double value(const Location & loc) override { return _val;}
private:
  double _val;
};

class IncrementQpValuer : public QpValuer<double>
{
public:
  virtual double value(const Location & loc) override { return _next++;}
private:
  int _next = 0;
};

class MyMat
{
public:
  MyMat(FEProblem& fep, std::string name, std::vector<std::string> props)
  {
    for (auto& prop : props)
    {
      _vars.push_back(new ConstQpValuer(_vars.size() + 42000));
      fep.registerProp(_vars.back(), name + "-" + prop);
    }
  }
private:
  std::vector<QpValuer<double>*> _vars;
};

void scalingStudy()
{
  unsigned int props_per_mat = 10;
  unsigned int n_mats = 10;
  unsigned int n_steps = 10;
  unsigned int n_quad_points = 1000000;
  unsigned int n_repeat_calcs = 5;

  FEProblem fep;

  std::vector<std::string> prop_names;
  for (int i = 0; i < props_per_mat; i++)
    prop_names.push_back("prop" + std::to_string(i+1));

  for (int i = 0; i < n_mats; i++)
    new MyMat(fep, "mat" + std::to_string(i+1), prop_names);

  std::vector<unsigned int> prop_ids;
  for (auto & prop : prop_names)
    for (int i = 0; i < n_mats; i++)
      prop_ids.push_back(fep.prop_id("mat" + std::to_string(i+1) + "-" + prop));

  for (int t = 0; t < n_steps; t++)
  {
    std::cout << "step " << t+1 << std::endl;
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

  std::cout << "mymat-prop1=" << fep.getProp<double>("mymat-prop1", Location(fep, 3, 1)) << std::endl;
  std::cout << "mymat-prop1=" << fep.getProp<double>("mymat-prop1", Location(fep, 3, 2)) << std::endl;
  std::cout << "mymat-prop7=" << fep.getProp<double>("mymat-prop7", Location(fep, 3, 2)) << std::endl;

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

int
main(int argc, char** argv)
{
  //scalingStudy();
  basicPrintoutTest();

  //FEProblem fep;
  //MyMat mat(fep, "mymat", {"prop1", "prop7"});
  //MyDepOldMat matdepold(fep, "mymatdepold", "mymat-prop7");

  //std::cout << fep.getProp<double>("mymat-prop1", Location(fep, 3, 1)) << std::endl;
  //std::cout << fep.getProp<double>("mymat-prop1", Location(fep, 3, 2)) << std::endl;
  //std::cout << fep.getProp<double>("mymat-prop7", Location(fep, 3, 2)) << std::endl;

  //std::cout << "printing older props:\n";
  //Location loc(fep, 1);
  //for (int i = 0; i < 8; i++)
  //{
  //  std::cout << "\nprop7=" << fep.getProp<double>("mymat-prop7", loc) << std::endl;
  //  std::cout << "    olderprop=" << fep.getProp<double>("mymatdepold", loc) << std::endl;
  //}

  return 0;
}

