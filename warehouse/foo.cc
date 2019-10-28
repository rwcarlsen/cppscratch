
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <memory>

class Attribute {};


class TheWarehouse
{
  public:
    size_t prepare(std::vector<std::unique_ptr<Attribute>> & attribs)
    {
      std::cout << "(running new query)";
      static size_t next_id = 0;
      return next_id++;
    }
};

template <typename T>
using KeyType = typename T::Key;
template <typename T>
using AttribType = T *;

template <typename ... Attribs>
class QueryCache
{
public:
  typedef std::tuple<KeyType<Attribs>...> KeyTuple;
  typedef std::tuple<AttribType<Attribs>...> AttribTuple;

  QueryCache(TheWarehouse & w) : _w(w)
  {
    addAttribs<0, Attribs...>();
  }

  template <typename T, typename... Args>
  QueryCache & baseCondition(Args &&... args)
  {
    _attribs.emplace_back(new T(std::forward<Args>(args)...));
    return *this;
  }

  template <typename... Args>
  size_t query_id(Args &&... args)
  {
    setKeysInner<0, KeyType<Attribs>...>(args...);
    if (_cache.count(_key_tup) > 0)
      return _cache[_key_tup];

    setAttribsInner<0, KeyType<Attribs>...>(args...);
    auto id = _w.prepare(_attribs);
    _cache[_key_tup] = id;
    return id;
  }

private:
  template <int Index, typename A, typename ... As>
  void
  addAttribs()
  {
    std::get<Index>(_attrib_tup) = new A();
    _attribs.emplace_back(std::get<Index>(_attrib_tup));
    addAttribs<Index+1, As...>();
  }
  template <int Index>
  void addAttribs() { }

  template <int Index, typename K, typename ... Args>
  void
  setKeysInner(K & k, Args &&... args)
  {
    std::get<Index>(_key_tup) = k;
    setKeysInner<Index+1, Args...>(args...);
  }
  template <int Index>
  void setKeysInner() { }


  template <int Index, typename K, typename ... Args>
  void
  setAttribsInner(K k, Args &&... args)
  {
    std::get<Index>(_attrib_tup)->setFrom(k);
    setAttribsInner<Index+1, Args...>(args...);
  }
  template <int Index>
  void setAttribsInner() { }

  AttribTuple _attrib_tup;
  KeyTuple _key_tup;
  std::map<KeyTuple, size_t> _cache;

  std::vector<std::unique_ptr<Attribute>> _attribs;
  TheWarehouse & _w;
};

class Attribute1 : public Attribute {
public:
  typedef int Key;
  void setFrom(Key k)
  {
    _val = k;
  }
  int _val = 0;
};

class Attribute2 : public Attribute {
public:
  typedef int Key;
  void setFrom(Key k)
  {
    _val = k;
  }
  int _val = 42;
};

class Attribute3 : public Attribute {
public:
  typedef std::string Key;
  void setFrom(Key k)
  {
    _val = k;
  }
  std::string _val;
};


int main(int argc, char** argv)
{
  TheWarehouse w;
  QueryCache<Attribute1, Attribute3> q(w);
  q.baseCondition<Attribute2>();

  std::cout << "key='hello': id=" << q.query_id(1, "hello") << "\n";;
  std::cout << "key='hello': id=" << q.query_id(2, "hello") << "\n";;
  std::cout << "key='foo': id=" << q.query_id(1, "foo") << "\n";;
  std::cout << "key='hello': id=" << q.query_id(1, "hello") << "\n";;
  return 0;
}

