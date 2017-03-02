
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

class Material
{
public:
  // calls FEProblem::registerMatProp(this, "[prop-name]") for each property
  Material() { };

  virtual void compute(const Location& loc) = 0;
};

class MatPropStore
{
public:
  inline unsigned int prop_id(const std::string& prop)
  {
    if (_prop_ids.count(prop) == 0)
      throw std::runtime_error("material property " + prop + " doesn't exist");
    return _prop_ids[prop];
  }

  template <typename T>
  unsigned int registerProp(Material* mat, T* var, const std::string& prop) {
    unsigned int id = _props_other.size();
    _prop_ids[prop] = id;
    _mats_other.push_back(mat);
    _computed_other.push_back(false);
    _props_other.push_back(var);
    return id;
  }

  template <typename T>
  T getProp(unsigned int prop, const Location& loc)
  {
    if (_computed_other[prop])
      return *dynamic_cast<T*>(_props_other[prop]);
    _mats_other[prop]->compute(loc);
    _computed_other[prop] = true;
    return *dynamic_cast<T*>(_props_other[prop]);
  }

  template <typename T>
  inline double getProp(const std::string& prop, const Location& loc)
  {
    if (_prop_ids.count(prop) == 0 || _props.size()-1 < _prop_ids[prop])
      throw std::runtime_error("material property " + prop + " doesn't exist");
    return getProp<T>(_prop_ids[prop], loc);
  }

  void clearCache()
  {
    for (int i = 0; i < _computed.size(); i++)
      _computed[i] = false;
    for (int i = 0; i < _computed_vec.size(); i++)
      _computed_vec[i] = false;
    for (int i = 0; i < _computed_other.size(); i++)
      _computed_other[i] = false;
  }

private:
  std::map<std::string, unsigned int> _prop_ids;

  std::vector<Material*> _mats;
  std::vector<Material*> _mats_vec;
  std::vector<Material*> _mats_other;

  std::vector<bool> _computed;
  std::vector<bool> _computed_vec;
  std::vector<bool> _computed_other;

  std::vector<double*> _props;
  std::vector<std::vector<double>*> _props_vec;
  std::vector<void*> _props_other;
};

template <>
unsigned int MatPropStore::registerProp(Material* mat, double* var, const std::string& prop) {
  unsigned int id = _props.size();
  _prop_ids[prop] = id;
  _mats.push_back(mat);
  _computed.push_back(false);
  _props.push_back(var);
  return id;
}

template <>
unsigned int MatPropStore::registerProp(Material* mat, std::vector<double>* var, const std::string& prop) {
  unsigned int id = _props_vec.size();
  _prop_ids[prop] = id;
  _mats_vec.push_back(mat);
  _computed_vec.push_back(false);
  _props_vec.push_back(var);
  return id;
}

template <>
double MatPropStore::getProp(unsigned int prop, const Location& loc)
{
  if (_computed[prop])
    return *_props[prop];
  _mats[prop]->compute(loc);
  _computed[prop] = true;
  return *_props[prop];
}

template <>
std::vector<double>& MatPropStore::getProp(unsigned int prop, const Location& loc)
{
  if (_computed_vec[prop])
    return *_props_vec[prop];
  _mats_vec[prop]->compute(loc);
  _computed_vec[prop] = true;
  return *_props_vec[prop];
}

class FEProblem
{
public:
  template <typename T>
  inline void registerMatProp(Material* mat, T* var, const std::string& prop) { _propstore.registerProp<T>(mat, var, prop); }

  template <typename T>
  inline T getMatProp(const std::string& prop, const Location& loc) {return _propstore.getProp<T>(prop, loc);}
  template <typename T>
  inline T getMatProp(unsigned int prop, const Location& loc) {return _propstore.getProp<T>(prop, loc);}

  inline void clearCache() { _propstore.clearCache(); }

  inline unsigned int prop_id(const std::string& prop) { return _propstore.prop_id(prop); }

private:
  MatPropStore _propstore;
};

template <typename T>
class MeshStore
{
public:
  void storeProp(const Location& loc, const std::string& prop)
  {
    resize(loc)[loc.qp()] = loc.fep().getMatProp<T>(prop, loc);
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

class MyDepOldMat : public Material
{
public:
  MyDepOldMat(FEProblem& fep, std::string prop, std::string old_dep_prop) : _fep(fep), _old_dep_prop(old_dep_prop)
  {
    fep.registerMatProp(this, &_prop, prop);
  }

  virtual void compute(const Location& loc) override
  {
    _prop = _older_vars.retrieve(loc);
    _older_vars.store(loc, _old_vars);
    _old_vars.storeProp(loc, _old_dep_prop);
  }

private:
  FEProblem& _fep;
  double _prop;
  std::string _old_dep_prop;
  MeshStore<double> _old_vars;
  MeshStore<double> _older_vars;
};

class MyMat : public Material
{
public:
  MyMat(FEProblem& fep, std::string name, std::vector<std::string> props)
  {
    for (auto& prop : props)
    {
      _var.push_back((_var.size()+1)*100000);
      fep.registerMatProp(this, &_var.back(), name + "-" + prop);
    }
  }

  virtual void compute(const Location& loc) override
  {
    for (int i = 0; i < _var.size(); i++)
      _var[i] += loc.qp();
  }
private:
  std::vector<double> _var;
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
        fep.clearCache(); // must be cleared before looping over properties and inside quad points
        for (auto & prop : prop_ids)
          fep.getMatProp<double>(prop, Location(fep, i));
      }
    }
  }
};

int
main(int argc, char** argv)
{
  //scalingStudy();

  FEProblem fep;
  MyMat mat(fep, "mymat", {"prop1", "prop7"});
  MyDepOldMat matdepold(fep, "mymatdepold", "mymat-prop7");

  std::cout << fep.getMatProp<double>("mymat-prop1", Location(fep, 1)) << std::endl;
  std::cout << fep.getMatProp<double>("mymat-prop1", Location(fep, 2)) << std::endl;
  std::cout << fep.getMatProp<double>("mymat-prop7", Location(fep, 2)) << std::endl;

  std::cout << "printing older props:\n";
  Location loc(fep, 1);
  for (int i = 0; i < 8; i++)
  {
    fep.clearCache();
    std::cout << "\nprop7=" << fep.getMatProp<double>("mymat-prop7", loc) << std::endl;
    std::cout << "    olderprop=" << fep.getMatProp<double>("mymatdepold", loc) << std::endl;
  }

  return 0;
}

