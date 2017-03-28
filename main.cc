
#include <iostream>
#include <map>
#include <vector>
#include <list>

#include "valuer.h"
#include "mock.h"

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

class MyMat : public Material
{
public:
  MyMat(FEProblem & fep, std::string base, std::vector<std::string> props) : Material(fep)
  {
    for (int i = 0; i < props.size(); i++)
      registerPropFunc<double>(base + "-" + props[i], [=](const Location& loc){return 42000+i;});
  }
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
