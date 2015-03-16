#ifndef HQW_DATAWRAPPER_HPP
#define HQW_DATAWRAPPER_HPP

namespace hqw
{
  template <typename T, int ID=1>
  class DataWrapper
  {
    public:
      DataWrapper() = default;
    protected:
      T data{};
  };
}

#endif
