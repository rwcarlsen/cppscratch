
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

class Location
{
public:
  Location(unsigned int qp) : _qp(qp) { }
  unsigned int qp() const {return _qp;}
  Point point() const {return {1, 2, 5};}
  Elem* elem() const {return nullptr;}
  Node* node() const {return nullptr;}
private:
  unsigned int _qp;
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
  unsigned int registerMatProp(Material* mat, double* var, const std::string& prop) {
    unsigned int id = _props.size();
    _prop_ids[prop] = id;
    _mats.push_back(mat);
    _props.push_back(var);
    return id;
  }

  unsigned int registerMatPropVec(Material* mat, std::vector<double>* var, const std::string& prop) {
    unsigned int id = _props_vec.size();
    _prop_ids[prop] = id;
    _mats_vec.push_back(mat);
    _props_vec.push_back(var);
    return id;
  }

  inline unsigned int prop_id(const std::string& prop)
  {
    if (_prop_ids.count(prop) == 0)
      throw std::runtime_error("material property " + prop + " doesn't exist");
    return _prop_ids[prop];
  }

  double getMatProp(unsigned int prop, const Location& loc)
  {
    _mats[prop]->compute(loc);
    return *_props[prop];
  }

  std::vector<double>& getMatPropVec(unsigned int prop, const Location& loc)
  {
    _mats_vec[prop]->compute(loc);
    return *_props_vec[prop];
  }

  inline double getMatProp(const std::string& prop, const Location& loc)
  {
    if (_prop_ids.count(prop) == 0 || _props.size()-1 < _prop_ids[prop])
    {
      throw std::runtime_error("material property " + prop + " doesn't exist");
    }
    return getMatProp(_prop_ids[prop], loc);
  }

  inline std::vector<double>& getMatPropVec(const std::string& prop, const Location& loc)
  {
    if (_prop_ids.count(prop) == 0 || _props_vec.size()-1 < _prop_ids[prop])
      throw std::runtime_error("material property " + prop + " doesn't exist");
    return getMatPropVec(_prop_ids[prop], loc);
  }

  void clearCache() {
  }

private:
  std::map<std::string, unsigned int> _prop_ids;

  std::vector<Material*> _mats;
  std::vector<Material*> _mats_vec;

  std::vector<double*> _props;
  std::vector<std::vector<double>*> _props_vec;
};

class FEProblem
{
public:
  inline void registerMatProp(Material* mat, double* var, const std::string& prop) { _propstore.registerMatProp(mat, var, prop); }
  inline void registerMatPropVec(Material* mat, std::vector<double>* var, const std::string& prop) { _propstore.registerMatPropVec(mat, var, prop); }

  inline double getMatProp(const std::string& prop, const Location& loc) {return _propstore.getMatProp(prop, loc);}
  inline std::vector<double> getMatPropVec(const std::string& prop, const Location& loc) {return _propstore.getMatPropVec(prop, loc);}
  inline double getMatProp(unsigned int prop, const Location& loc) {return _propstore.getMatProp(prop, loc);}
  inline std::vector<double> getMatPropVec(unsigned int prop, const Location& loc) {return _propstore.getMatPropVec(prop, loc);}

  inline void clearCache() { _propstore.clearCache(); }

  inline unsigned int prop_id(const std::string& prop) { return _propstore.prop_id(prop); }

private:
  MatPropStore _propstore;
};

class MyMat : public Material
{
public:
  MyMat(FEProblem& fep, std::string name, std::vector<std::string> props)
  {
    for (auto& prop : props)
    {
      _var.push_back(0);
      fep.registerMatProp(this, &_var.back(), name + "-" + prop);
    }
  }

  virtual void compute(const Location& loc) override
  {
    for (int i = 0; i < _var.size(); i++)
      _var[i] = i*100000+loc.qp();
  }
private:
  std::vector<double> _var;
};

int
main(int argc, char** argv)
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
    fep.clearCache();
    for (int i = 0; i < n_quad_points; i++)
    {
      for (auto & prop : prop_ids)
      {
        for (int rep = 0; rep < n_repeat_calcs; rep++)
          fep.getMatProp(prop, Location(i));
      }
    }
  }

  {
    FEProblem fep;
    MyMat mat(fep, "mymat", {"prop1", "prop7"});

    std::cout << fep.getMatProp("mymat-prop1", Location(1)) << std::endl;
    std::cout << fep.getMatProp("mymat-prop1", Location(2)) << std::endl;
    std::cout << fep.getMatProp("mymat-prop7", Location(2)) << std::endl;
  }

  return 0;
}

