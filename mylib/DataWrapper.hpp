#ifndef HQW_DATAWRAPPER_HPP
#define HQW_DATAWRAPPER_HPP

namespace hqw
{
  template <typename T, int ID=1>
  class DataWrapper
  {
    public:
      DataWrapper() = default;
      explicit DataWrapper(T d)
        : data(std::move(d))
      {}
    protected:
      T data{};
  };
}

#endif
