#ifndef _HQW_DAG_IDAG_H_
#define _HQW_DAG_IDAG_H_

#include <string>

namespace hqw { namespace impl {

class IDAG
{
  public:
    virtual ~IDAG () {}

    virtual IDAG* clone() = 0;
    virtual void addRow() = 0;
    virtual size_t getRowCount() = 0;
    // set
    virtual void setValue(unsigned int row, unsigned int col, const std::string& val) = 0;
    virtual void setValue(unsigned int row, unsigned int col, long val) = 0;
    virtual void setValue(unsigned int row, unsigned int col, double val) = 0;
    // get
    virtual void getValue(unsigned int row, unsigned int col, std::string& val) = 0;
    virtual void getValue(unsigned int row, unsigned int col, long& val) = 0;
    virtual void getValue(unsigned int row, unsigned int col, double& val) = 0;
};  

}}

#endif

