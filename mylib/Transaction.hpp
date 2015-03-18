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

    void commit();
    void rollback();

private:

    void doRollback(T&&);
    void doRollback(const T&);
    void createBackup();

    std::unique_ptr<T> m_pObj;
    T& m_obj;
};

template <typename T, typename Traits>
Transaction<T, Traits>::Transaction(T& obj)
    : m_pObj(std::unique_ptr<T>(new T())), m_obj(obj)
{
    createBackup();
}


template <typename T, typename Traits>
Transaction<T, Traits>::~Transaction()
{
    try
    {
        doRollback(std::move(*m_pObj));
    }
    catch (...)
    {
    }
}

template <typename T, typename Traits>
void Transaction<T, Traits>::commit()
{
    createBackup();
}


template <typename T, typename Traits>
void Transaction<T, Traits>::rollback()
{
    doRollback(*m_pObj);
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
    Traits::backup(m_obj, *m_pObj);
}

}

#endif
