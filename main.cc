
#include <iostream>
#include <map>
#include <vector>
#include <list>

#include "valuer.h"
#include "moose.h"

class ConstValuer : public Valuer<double>
{
public:
  ConstValuer(double val) : _val(val) {}
  virtual double get(const Location & loc) override { return _val; }

private:
  double _val;
};

class IncrementValuer : public Valuer<double>
{
public:
  virtual double get(const Location & loc) override { return _next++; }

private:
  int _next = 0;
};

class DepValuer : public Valuer<double>
{
public:
  DepValuer(QpStore & qs, double toadd, const std::string & dep) : _toadd(toadd), _dep(dep), _qs(qs)
  {
  }
  virtual double get(const Location & loc) override
  {
    return _qs.get<double>(_dep, loc) + _toadd;
  }

private:
  QpStore & _qs;
  double _toadd;
  std::string _dep;
};

class MyMat : public Material
{
public:
  MyMat(FEProblem & fep, std::string base, std::vector<std::string> props) : Material(fep)
  {
    for (int i = 0; i < props.size(); i++)
      addPropFunc<double>(base + "-" + props[i], [=](const Location & loc) { return 42000 + i; });
  }
};

class DemoMaterial : public Material
{
public:
  DemoMaterial(FEProblem & fep, std::set<BlockId> blocks = {}) : Material(fep, blocks)
  {
    bind_prop_func("demo-prop1", prop1, double);
    bind_prop_func("demo-prop2", prop2, double);

    // or maybe you want to calculate several properties together
    bind_prop("demo-prop-a", propABC, _a);
    bind_prop("demo-prop-b", propABC, _b);
    bind_prop("demo-prop-c", propABC, _c);
  }

  double prop1(const Location & loc) { return 42; }

  double prop2(const Location & loc)
  {
    return 42 * prop<double>("demo-prop1", loc);
    // you could also do the following for the same result:
    //    return 42 * prop1(loc);
  }

  void propABC(const Location & loc)
  {
    _a = prop<double>("prop-from-another-material", loc, {"isotropic-guarantee"});
    _b = 2 * _a;
    _c = 2 * _b;
  }

private:
  double _a;
  double _b;
  double _c;
};

class DemoMaterial2 : public Material
{
public:
  DemoMaterial2(FEProblem & fep, std::set<BlockId> blocks = {}) : Material(fep, blocks)
  {
    bind_prop_func("prop-from-another-material", prop1, double); //, "isotropic-guarantee");
  }
  double prop1(const Location & loc) { return 42; }
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

  std::vector<ValId> prop_ids;
  for (auto & prop : prop_names)
    for (int i = 0; i < n_mats; i++)
      prop_ids.push_back(fep.props().id("mat" + std::to_string(i + 1) + "-" + prop));

  for (int t = 0; t < n_steps; t++)
  {
    std::cout << "step " << t + 1 << std::endl;
    for (int rep = 0; rep < n_repeat_calcs; rep++)
    {
      for (int i = 0; i < n_quad_points; i++)
      {
        for (auto & prop : prop_ids)
          fep.props().get<double>(prop, Location(n_quad_points, i));
      }
    }
  }
};

void
customKeyTest()
{
  class ByNode : public Value
  {
  public:
    ByNode(unsigned int i) : my_special_id(i) {}
    virtual bool lessThan(const Value & rhs)
    {
      return my_special_id < static_cast<const ByNode &>(rhs).my_special_id;
    }
    unsigned int my_special_id = 0;
  };

  std::cout << "**** customKeyTest ****\n";
  FEProblem fep;
  fep.props().add("prop1", new ConstValuer(7), true);
  fep.props().add("prop2", new ConstValuer(42), true);
  fep.props().wantOld("prop1");
  fep.props().wantOld("prop2");

  Location loc1(1, 1);
  loc1.custom.reset(new ByNode(1));
  Location loc2(1, 1);
  loc2.custom.reset(new ByNode(2));

  std::cout << "prop1=" << fep.props().get<double>("prop1", loc1) << std::endl;
  std::cout << "prop2=" << fep.props().get<double>("prop2", loc2) << std::endl;
  std::cout << "shift()\n";
  fep.props().shift();
  std::cout << "prop1_old=" << fep.props().getOld<double>("prop1", loc1) << std::endl;
  std::cout << "prop2_old=" << fep.props().getOld<double>("prop2", loc2) << std::endl;
}

void
basicPrintoutTest()
{
  FEProblem fep;
  MyMat mat(fep, "mymat", {"prop1", "prop7"});

  std::cout << "mymat-prop1=" << fep.props().get<double>("mymat-prop1", Location(3, 1))
            << std::endl;
  std::cout << "mymat-prop1=" << fep.props().get<double>("mymat-prop1", Location(3, 2))
            << std::endl;
  std::cout << "mymat-prop7=" << fep.props().get<double>("mymat-prop7", Location(3, 2))
            << std::endl;

  IncrementValuer iq;
  auto id = fep.props().add("inc-qp", &iq);

  std::cout << "inc-qp=" << fep.props().get<double>(id, Location(1, 0)) << std::endl;
  std::cout << "  old inc-qp=" << fep.props().getOld<double>(id, Location(1, 0)) << std::endl;
  std::cout << "--- shift\n";
  fep.props().shift();
  std::cout << "inc-qp=" << fep.props().get<double>(id, Location(1, 0)) << std::endl;
  std::cout << "  old inc-qp=" << fep.props().getOld<double>(id, Location(1, 0)) << std::endl;
  std::cout << "--- shift\n";
  fep.props().shift();
  std::cout << "inc-qp=" << fep.props().get<double>(id, Location(1, 0)) << std::endl;
  std::cout << "  old inc-qp=" << fep.props().getOld<double>(id, Location(1, 0)) << std::endl;
  std::cout << "inc-qp=" << fep.props().get<double>(id, Location(1, 0)) << std::endl;
  std::cout << "  old inc-qp=" << fep.props().getOld<double>(id, Location(1, 0)) << std::endl;
  std::cout << "--- shift\n";
  fep.props().shift();
  std::cout << "inc-qp=" << fep.props().get<double>(id, Location(1, 0)) << std::endl;
  std::cout << "  old inc-qp=" << fep.props().getOld<double>(id, Location(1, 0)) << std::endl;
}

void
wrongTypeTest()
{
  FEProblem fep(true);
  MyMat mat(fep, "mymat", {"prop1", "prop7"});
  // throw error - wrong type.
  try
  {
    fep.props().get<int>("mymat-prop1", Location(0, 1));
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
  DepValuer dq1(fep.props(), 1, "dep2");
  DepValuer dq2(fep.props(), 1, "dep3");
  DepValuer dq3(fep.props(), 1, "dep1");
  auto id1 = fep.props().add("dep1", &dq1);
  auto id2 = fep.props().add("dep2", &dq2);
  auto id3 = fep.props().add("dep3", &dq3);

  // throw error - cyclical dependency
  try
  {
    fep.props().get<double>(id1, Location(0, 1));
  }
  catch (std::runtime_error err)
  {
    std::cout << err.what() << std::endl;
    return;
  }
  std::cout << "cyclicalDepTest FAIL\n";
}

void
blockRestrictDemo()
{
  // this code would all be done automagically by moose from input file as normal
  FEProblem fep;
  ConstValuer v1(42);
  ConstValuer v2(43);
  fep.props().add("v1", &v1);
  fep.props().add("v2", &v2);

  // User wanting to switch properties based on block would need to write sth like this:
  LambdaValuer<double> v;
  v.init([&fep](const Location & loc) {
    if (loc.block_id > 5)
      return fep.props().get<double>("v2", loc);
    return fep.props().get<double>("v1", loc);
  });
  fep.props().add("v", &v);

  // test printout code should show:
  //     42
  //     42
  //     43
  //     43
  BlockId block_id = 4;
  std::cout << fep.props().get<double>("v", Location(3, 1, 1, block_id)) << std::endl;
  block_id++;
  std::cout << fep.props().get<double>("v", Location(3, 1, 1, block_id)) << std::endl;
  block_id++;
  std::cout << fep.props().get<double>("v", Location(3, 1, 1, block_id)) << std::endl;
  block_id++;
  std::cout << fep.props().get<double>("v", Location(3, 1, 1, block_id)) << std::endl;

  // or you can use a convenience umbrella material like this that would normally be
  // created/initialized automatigically from the input file
  // (i.e. [Material] type=Umbrella; prop="vv"; subprop='v1 0 1 2 3 4'; etc.):
  Umbrella um(fep, "vv", {{"v1", {0, 1, 2, 3, 4, 5}}, {"v2", {6, 7, 8}}});
  // test printout code should show:
  //     42
  //     42
  //     43
  //     43
  block_id = 4;
  std::cout << fep.props().get<double>("vv", Location(3, 1, 1, block_id)) << std::endl;
  block_id++;
  std::cout << fep.props().get<double>("vv", Location(3, 1, 1, block_id)) << std::endl;
  block_id++;
  std::cout << fep.props().get<double>("vv", Location(3, 1, 1, block_id)) << std::endl;
  block_id++;
  std::cout << fep.props().get<double>("vv", Location(3, 1, 1, block_id)) << std::endl;

  // or you can use a convenience blocks param arg to the material constructor.  This is more
  // analogous to current moose practice where you manually block-restrict each material object,
  // but is not necessary for performance-only cases and is only necessary for multiple material
  // objects mapped to one property name cases - in which case it is more natural/simple to put
  // the all the mappings together using e.g. an Umbrella material class.
  DemoMaterial dm1(fep, {0, 1, 2, 3, 4, 5});
  DemoMaterial dm2(fep, {6, 7, 8});
}

void
guaranteesTest()
{
  // dm2 provides  the external property "prop-from-another-material", but without the guarantee
  // "isotropic-guarantee" that is asked for on it.
  FEProblem fep(true);
  DemoMaterial dm1(fep);
  DemoMaterial2 dm2(fep);

  try
  {
    fep.props().get<double>("demo-prop-a", Location(1, 1));
  }
  catch (std::runtime_error err)
  {
    std::cout << err.what() << std::endl;
    return;
  }
  std::cout << "guaranteesTest FAIL\n";
}

int
main(int argc, char ** argv)
{
  scalingStudy();
  basicPrintoutTest();
  customKeyTest();
  wrongTypeTest();
  cyclicalDepTest();
  blockRestrictDemo();
  guaranteesTest();

  // FEProblem fep;
  // MyMat mat(fep, "mymat", {"prop1", "prop7"});
  // MyDepOldMat matdepold(fep, "mymatdepold", "mymat-prop7");

  // std::cout << fep.props().get<double>("mymat-prop1", Location(3, 1)) << std::endl;
  // std::cout << fep.props().get<double>("mymat-prop1", Location(3, 2)) << std::endl;
  // std::cout << fep.props().get<double>("mymat-prop7", Location(3, 2)) << std::endl;

  // std::cout << "printing older props:\n";
  // Location loc(fep, 1);
  // for (int i = 0; i < 8; i++)
  //{
  //  std::cout << "\nprop7=" << fep.props().get<double>("mymat-prop7", loc) << std::endl;
  //  std::cout << "    olderprop=" << fep.props().get<double>("mymatdepold", loc) << std::endl;
  //}
  //

  return 0;
}
