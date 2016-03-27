#ifndef _HQW_DAG_H_
#define _HQW_DAG_H_

#include <string>
#include <memory>


namespace hqw {

namespace impl 
{
    class IDAG;
}

class DAG
{
  public:
    explicit DAG(const char * tableID);
    ~DAG();

    // copy
    DAG(const DAG&);
    DAG& operator=(const DAG&);

    // move
    DAG(DAG&&) noexcept;
    DAG& operator = (DAG&&);

    template <typename T>
    void setValue(unsigned int row, unsigned int col, const T& val)
    {
      _setValue(row, col, val);
    }
    template <typename T>
    void getValue(unsigned int row, unsigned int col, T& val) const
    {
      _getValue(row, col, val);
    }

    void addRow();

    size_t getRowCount() const;

  private:
    void _setValue(unsigned int row, unsigned int col, const std::string& val);
    void _setValue(unsigned int row, unsigned int col, long val);
    void _getValue(unsigned int row, unsigned int col, std::string& val) const;
    void _getValue(unsigned int row, unsigned int col, long& val) const;

    std::unique_ptr<impl::IDAG> m_impl;
};

}

#endif
