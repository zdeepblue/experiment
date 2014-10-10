#ifndef HQW_DATAWRAPPER_HPP
#define HQW_DATAWRAPPER_HPP

namespace hqw
{
  template <typename T>
  class DataWrapper
  {
    public:
      DataWrapper()
      {
        data = T();
      }
      T data;
  };
}

#endif
