#ifndef HQW_TRANSACTION_HPP
#define HQW_TRANSACTION_HPP

namespace hqw
{

template <typename T>    
class DefaultTransTraits    
{
public:
    static void backup(const T& from, T& to)
    {
      to = from;
    }
    static void backup(T&& from, T& to)
    {
        to = std::move(from);
    }
};

template <typename T, typename Traits = DefaultTransTraits<T> >
class Transaction 
{
public:
    Transaction(T& obj);
    ~Transaction();

    void commit(bool endOfTrans=false);
    void rollback(bool endOfTrans=false);

private:

    void doRollback(T&&);
    void doRollback(const T&);
    void createBackup();

    T m_bakObj;
    T& m_obj;
    bool m_endOfTrans{false};
};

template <typename T, typename Traits>
Transaction<T, Traits>::Transaction(T& obj)
    : m_bakObj(obj), m_obj(obj)
{
}


template <typename T, typename Traits>
Transaction<T, Traits>::~Transaction()
{
    if (!m_endOfTrans)
    {
      try
      {
        doRollback(std::move(m_bakObj));
      }
      catch (...)
      {
      }
    }
}

template <typename T, typename Traits>
void Transaction<T, Traits>::commit(bool endOfTrans)
{
    if (m_endOfTrans) return;
    m_endOfTrans = endOfTrans;
    if (!endOfTrans) createBackup();
}


template <typename T, typename Traits>
void Transaction<T, Traits>::rollback(bool endOfTrans)
{
    if (m_endOfTrans) return;
    m_endOfTrans = endOfTrans;
    (endOfTrans) ? doRollback(std::move(m_bakObj)) : doRollback(m_bakObj);
}

template <typename T, typename Traits>
void Transaction<T, Traits>::doRollback(T&& obj)
{
    Traits::backup(std::move(obj), m_obj);
}

template <typename T, typename Traits>
void Transaction<T, Traits>::doRollback(const T& obj)
{
    Traits::backup(obj, m_obj);
}

template <typename T, typename Traits>
void Transaction<T, Traits>::createBackup()
{
    Traits::backup(m_obj, m_bakObj);
}

}

#endif
