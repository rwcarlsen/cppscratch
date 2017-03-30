
#include <iostream>
#include <map>
#include <vector>
#include <list>

#include "valuer.h"
#include "mock.h"
#include "util.h"

class ConstQpValuer : public QpValuer<double>
{
public:
  ConstQpValuer(double val) : _val(val) {}
  virtual double get(const Location & loc) override { return _val; }

private:
  double _val;
};

class IncrementQpValuer : public QpValuer<double>
{
public:
  virtual double get(const Location & loc) override { return _next++; }

private:
  int _next = 0;
};

class DepQpValuer : public QpValuer<double>
{
public:
  DepQpValuer(double toadd, const std::string & dep) : _toadd(toadd), _dep(dep) {}
  virtual double get(const Location & loc) override
  {
    return loc.vals().get<double>(_dep, loc) + _toadd;
  }

private:
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
  DemoMaterial(FEProblem & fep) : Material(fep)
  {
    bind_prop_func("demo-prop1", double, prop1);
    bind_prop_func("demo-prop2", double, prop2);

    // or maybe you want to calculate several properties together
    bind_prop_func_var("demo-prop-a", double, propABC, _a);
    bind_prop_func_var("demo-prop-b", double, propABC, _b);
    bind_prop_func_var("demo-prop-c", double, propABC, _c);
  }

  double prop1(const Location& loc) { return 42; }

  double prop2(const Location& loc)
  {
    return 42 * loc.vals().get<double>("demo-prop1", loc);
    // you could obviously do the following for the same result:
    //    return 42 * prop1(loc);
  }

  void propABC(const Location& loc)
  {
    _a = loc.vals().get<double>("prop-from-another-material", loc);
    _b = 2*_a;
    _c = 2*_b;
  }

private:
  double _a;
  double _b;
  double _c;
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
  auto id = fep.addProp(&iq, "inc-qp");

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
  auto id1 = fep.addProp(&dq1, "dep1");
  auto id2 = fep.addProp(&dq2, "dep2");
  auto id3 = fep.addProp(&dq3, "dep3");

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

void
blockRestrictDemo()
{
  // this code would all be done automagically by moose from input file as normal
  FEProblem fep;
  ConstQpValuer v1(42);
  ConstQpValuer v2(43);
  fep.addProp(&v1, "v1");
  fep.addProp(&v2, "v2");

  // User wanting to switch properties based on block would need to write sth like this:
  LambdaValuer<double> v;
  v.init([&fep](const Location & loc) {
    if (loc.block() > 5)
      return fep.getProp<double>("v2", loc);
    return fep.getProp<double>("v1", loc);
  });
  fep.addProp(&v, "v");

  // test printout code should show:
  //     42
  //     42
  //     43
  //     43
  unsigned int block_id = 4;
  std::cout << fep.getProp<double>("v", Location(fep, 3, 1, 1, 0, block_id)) << std::endl;
  block_id++;
  std::cout << fep.getProp<double>("v", Location(fep, 3, 1, 1, 0, block_id)) << std::endl;
  block_id++;
  std::cout << fep.getProp<double>("v", Location(fep, 3, 1, 1, 0, block_id)) << std::endl;
  block_id++;
  std::cout << fep.getProp<double>("v", Location(fep, 3, 1, 1, 0, block_id)) << std::endl;

  // or you can use a convenience umbrella material like this that would normally be initialized
  // automatigically from the input file
  // (i.e. [Material] type=Umbrella; prop="vv"; subprop='v1 0 1 2 3 4'; etc.):
  Umbrella um(fep, "vv", {{"v1", {0, 1, 2, 3, 4, 5}}, {"v2", {6, 7, 8}}});
  // test printout code should show:
  //     42
  //     42
  //     43
  //     43
  block_id = 4;
  std::cout << fep.getProp<double>("vv", Location(fep, 3, 1, 1, 0, block_id)) << std::endl;
  block_id++;
  std::cout << fep.getProp<double>("vv", Location(fep, 3, 1, 1, 0, block_id)) << std::endl;
  block_id++;
  std::cout << fep.getProp<double>("vv", Location(fep, 3, 1, 1, 0, block_id)) << std::endl;
  block_id++;
  std::cout << fep.getProp<double>("vv", Location(fep, 3, 1, 1, 0, block_id)) << std::endl;
}

int
main(int argc, char ** argv)
{
  scalingStudy();
  basicPrintoutTest();
  wrongTypeTest();
  cyclicalDepTest();
  blockRestrictDemo();

  // FEProblem fep;
  // MyMat mat(fep, "mymat", {"prop1", "prop7"});
  // MyDepOldMat matdepold(fep, "mymatdepold", "mymat-prop7");

  // std::cout << fep.getProp<double>("mymat-prop1", Location(fep, 3, 1)) << std::endl;
  // std::cout << fep.getProp<double>("mymat-prop1", Location(fep, 3, 2)) << std::endl;
  // std::cout << fep.getProp<double>("mymat-prop7", Location(fep, 3, 2)) << std::endl;

  // std::cout << "printing older props:\n";
  // Location loc(fep, 1);
  // for (int i = 0; i < 8; i++)
  //{
  //  std::cout << "\nprop7=" << fep.getProp<double>("mymat-prop7", loc) << std::endl;
  //  std::cout << "    olderprop=" << fep.getProp<double>("mymatdepold", loc) << std::endl;
  //}
  //

  return 0;
}
