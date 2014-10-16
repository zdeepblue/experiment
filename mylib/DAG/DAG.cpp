#include "DAG.h"
#include "DAGImpl.hpp"

namespace hqw {


DAG::DAG(const char * tableID)
  : m_impl(hqw::impl::DAGImplManager::createInstance(tableID))
{
}

DAG::~DAG()
{
  delete m_impl;
}

void DAG::_setValue(unsigned int row, unsigned int col, long val)
{
  assert(m_impl != NULL);
  m_impl->setValue(row, col, val);
}
void DAG::_setValue(unsigned int row, unsigned int col, const std::string& val)
{
  assert(m_impl != NULL);
  m_impl->setValue(row, col, val);
}
void DAG::_getValue(unsigned int row, unsigned int col, long& val) const
{
  assert(m_impl != NULL);
  m_impl->getValue(row, col, val);
}
void DAG::_getValue(unsigned int row, unsigned int col, std::string& val) const
{
  assert(m_impl != NULL);
  m_impl->getValue(row, col, val);
}

void DAG::addRow()
{
  assert(m_impl != NULL);
  m_impl->addRow();
}

size_t DAG::getRowCount() const
{
  assert(m_impl != NULL);
  return m_impl->getRowCount();
}

DAG DAG::duplicate() const
{
  assert(m_impl != NULL);
  return DAG(m_impl->clone());
}

}

