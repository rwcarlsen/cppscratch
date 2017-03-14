
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
  Location(FEProblem& fep, unsigned int qp) : _qp(qp), _fep(fep) { }
  unsigned int qp() const {return _qp;}
  Point point() const {return {1, 2, 5};}
  Elem* elem() const {return nullptr;}
  Node* node() const {return nullptr;}
  FEProblem& fep() const {return _fep;}
private:
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
    return id;
  }

  template <typename T>
  T value(unsigned int name, const Location& loc)
  {
    return static_cast<QpValuer<T>*>(_valuers[name])->value(loc);
  }

  template <typename T>
  inline double value(const std::string& name, const Location& loc)
  {
    return value<T>(id(name), loc);
  }

private:
  std::map<std::string, unsigned int> _ids;
  std::vector<void*> _valuers;
};

class FEProblem
{
public:
  template <typename T>
  inline void registerProp(QpValuer<T>* v, const std::string& prop) { _propstore.registerValue<T>(v, prop); }

  template <typename T>
  inline T getProp(const std::string& name, const Location& loc) {return _propstore.value<T>(name, loc);}
  template <typename T>
  inline T getProp(unsigned int prop, const Location& loc) {return _propstore.value<T>(prop, loc);}

  inline unsigned int prop_id(const std::string& name) { return _propstore.id(name); }

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
          fep.getProp<double>(prop, Location(fep, i));
      }
    }
  }
};

void
basicPrintoutTest()
{
  FEProblem fep;
  MyMat mat(fep, "mymat", {"prop1", "prop7"});

  std::cout << fep.getProp<double>("mymat-prop1", Location(fep, 1)) << std::endl;
  std::cout << fep.getProp<double>("mymat-prop1", Location(fep, 2)) << std::endl;
  std::cout << fep.getProp<double>("mymat-prop7", Location(fep, 2)) << std::endl;
}

int
main(int argc, char** argv)
{
  //scalingStudy();
  basicPrintoutTest();

  //FEProblem fep;
  //MyMat mat(fep, "mymat", {"prop1", "prop7"});
  //MyDepOldMat matdepold(fep, "mymatdepold", "mymat-prop7");

  //std::cout << fep.getProp<double>("mymat-prop1", Location(fep, 1)) << std::endl;
  //std::cout << fep.getProp<double>("mymat-prop1", Location(fep, 2)) << std::endl;
  //std::cout << fep.getProp<double>("mymat-prop7", Location(fep, 2)) << std::endl;

  //std::cout << "printing older props:\n";
  //Location loc(fep, 1);
  //for (int i = 0; i < 8; i++)
  //{
  //  std::cout << "\nprop7=" << fep.getProp<double>("mymat-prop7", loc) << std::endl;
  //  std::cout << "    olderprop=" << fep.getProp<double>("mymatdepold", loc) << std::endl;
  //}

  return 0;
}

