#include "DAG.h"
#include "DAGImpl.hpp"

namespace hqw {


DAG::DAG(const char * tableID)
  : m_impl(hqw::impl::DAGImplManager::createInstance(tableID))
{
}

DAG::~DAG() = default;
DAG::DAG(DAG&&) noexcept = default;
DAG& DAG::operator=(DAG&&) = default;

DAG::DAG(const DAG& rhs)
  : m_impl(rhs.m_impl->clone())
{}

DAG& DAG::operator=(const DAG& rhs)
{
  if (this != &rhs) {
    m_impl.reset(rhs.m_impl->clone());
  }
  return *this;
}

void DAG::_setValue(unsigned int row, unsigned int col, long val)
{
  assert(m_impl != nullptr);
  m_impl->setValue(row, col, val);
}
void DAG::_setValue(unsigned int row, unsigned int col, const std::string& val)
{
  assert(m_impl != nullptr);
  m_impl->setValue(row, col, val);
}
void DAG::_getValue(unsigned int row, unsigned int col, long& val) const
{
  assert(m_impl != nullptr);
  m_impl->getValue(row, col, val);
}
void DAG::_getValue(unsigned int row, unsigned int col, std::string& val) const
{
  assert(m_impl != nullptr);
  m_impl->getValue(row, col, val);
}

void DAG::addRow()
{
  assert(m_impl != nullptr);
  m_impl->addRow();
}

size_t DAG::getRowCount() const
{
  assert(m_impl != nullptr);
  return m_impl->getRowCount();
}


}

