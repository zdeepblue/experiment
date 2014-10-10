#ifndef _HQW_DAG_IMPL_CELL_HPP_
#define _HQW_DAG_IMPL_CELL_HPP_

#include <string>
#include <boost/variant.hpp>
#include <boost/lexical_cast.hpp>

namespace hqw { namespace impl {

typedef boost::variant<boost::blank, long, double, std::string> DAGImplCell;

template <typename T>
class DAGImplCell_cast : public boost::static_visitor<T>
{
  public:
    T operator () (boost::blank) const
    {
      return T();
    }

    template <typename U>
    T operator () (const U& v) const
    {
      return boost::lexical_cast<T>(v);
    }
};


}}

#endif
