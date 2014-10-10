#ifndef _HQW_AUTOREGISTER_HPP_
#define _HQW_AUTOREGISTER_HPP_

namespace hqw {

template <typename T, typename R>
class AutoRegister
{
  public:
    ~AutoRegister()
    {
      bool registered = s_isRegistered;
    }

  private:
    static bool s_isRegistered;
};

template <typename T, typename R>
bool AutoRegister<T,R>::s_isRegistered = R()(static_cast<T*>(NULL));

}
#endif
