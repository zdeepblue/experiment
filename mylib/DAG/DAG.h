#ifndef _HQW_DAG_H_
#define _HQW_DAG_H_

#include <string>


namespace hqw {
  
namespace impl { class IDAG; }

class DAG
{
  public:
    DAG() : m_impl(NULL) {}
    explicit DAG(const char * tableID);
    ~DAG();

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

    void swap(DAG& other)
    {
      std::swap(m_impl, other.m_impl);
    }

    DAG duplicate() const;

  private:
    void _setValue(unsigned int row, unsigned int col, const std::string& val);
    void _setValue(unsigned int row, unsigned int col, long val);
    void _getValue(unsigned int row, unsigned int col, std::string& val) const;
    void _getValue(unsigned int row, unsigned int col, long& val) const;

    DAG(impl::IDAG* impl) : m_impl(impl) {}

    // noncopyable
    DAG(const DAG&);
    DAG& operator=(const DAG&);

    impl::IDAG* m_impl;
};

inline void swap(DAG& a, DAG& b)
{
  a.swap(b);
}

}

#endif
