#ifndef _HQW_DAG_IMPL_ROW_HPP_
#define _HQW_DAG_IMPL_ROW_HPP_

#include "DAGImplCell.hpp"

namespace hqw { namespace impl {
  
template <typename T, typename Traits>
class DAGImplRow
{
  private:
    DAGImplCell m_cells[Traits::COL_NUM];
  public:
    DAGImplCell& operator [] (unsigned int i)
    {
      return m_cells[i];
    }

    const DAGImplCell& operator [] (unsigned int i) const
    {
      return m_cells[i];
    }
};

}}

#endif
