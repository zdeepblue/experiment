#ifndef _HQW_DAG_IMPL_HPP_
#define _HQW_DAG_IMPL_HPP_

#include <boost/lexical_cast.hpp>
#include <deque>
#include "DAGImplRow.hpp"
#include "DAGImplManager.hpp"
#include "AutoRegister.hpp"
#include "IDAG.h"

namespace hqw { namespace impl {

struct DAGImplRegister
{
  template <typename T>
  bool operator () (T* ) const
  {
    return DAGImplManager::getInstance().registerDAGImpl<T>(T::getName());
  }
};

template <typename T, typename Traits>
class DAGImpl : public IDAG, private hqw::AutoRegister<T, DAGImplRegister>
{
  private:
    typedef std::deque<typename Traits::row_type> data_type;
    data_type m_data;
  protected:
    DAGImpl(const DAGImpl& rhs) : m_data(rhs.m_data) {}
    DAGImpl& operator = (const DAGImpl& rhs) { m_data = rhs.m_data;}

  public:
    DAGImpl() {}

    void addRow()
    {
       m_data.push_back(typename Traits::row_type());
    }

    size_t getRowCount()
    {
      return m_data.size();
    }

    void setValue(unsigned int row, unsigned int col, double value)
    {
      typename Traits::columns_type cols;
      cols.setColValue(col, value, m_data[row][col]);
    }

    void setValue(unsigned int row, unsigned int col, const std::string& value)
    {
      typename Traits::columns_type cols;
      cols.setColValue(col, value, m_data[row][col]);
    }

    void setValue(unsigned int row, unsigned int col, long value)
    {
      typename Traits::columns_type cols;
      cols.setColValue(col, value, m_data[row][col]);
    }

    void getValue(unsigned int row, unsigned int col, double& val)
    {
      typename Traits::columns_type cols;
      cols.getColValue(col, val, m_data[row][col]);
    }
    void getValue(unsigned int row, unsigned int col, std::string& val)
    {
      typename Traits::columns_type cols;
      cols.getColValue(col, val, m_data[row][col]);
    }
    void getValue(unsigned int row, unsigned int col, long& val)
    {
      typename Traits::columns_type cols;
      cols.getColValue(col, val, m_data[row][col]);
    }
};

}}

#define DEFINE_DAGIMPL_BEGIN(XXX)  \
  class XXX##ColTypes \
  { \
    public: \
      template <typename T> \
      void getColValue(unsigned int col, T& val, const hqw::impl::DAGImplCell& cell) \
      { \
        val = boost::apply_visitor(hqw::impl::DAGImplCell_cast<T>(), cell); \
      } \
      template <typename T> \
      void setColValue(unsigned int col, const T& val, hqw::impl::DAGImplCell& cell) \
      { \
        switch (col) \
        { \


#define MAP_COLUMN_TYPE(C, T) \
          case C: { cell = boost::lexical_cast<T>(val); break; } \

          
#define DEFINE_DAGIMPL_END(XXX, COLNUM) \
          default: break; \
        }\
      }\
  }; \
  class XXX##DAGImpl; \
  struct XXX##DAGImplTraits \
  { \
    typedef XXX##DAGImpl DAGImpl_type ; \
    typedef XXX##ColTypes columns_type; \
    typedef hqw::impl::DAGImplRow<DAGImpl_type, XXX##DAGImplTraits> row_type; \
    enum {COL_NUM = COLNUM}; \
  }; \
  class XXX##DAGImpl: public hqw::impl::DAGImpl<XXX##DAGImpl, XXX##DAGImplTraits> \
  { \
    public: \
      static const char * getName() { return #XXX ; } \
      ~ XXX##DAGImpl() {} \
      XXX##DAGImpl* clone() \
      { \
        XXX##DAGImpl* pCopy = new XXX##DAGImpl(*this); \
        return pCopy; \
      } \
  }; \

#endif

