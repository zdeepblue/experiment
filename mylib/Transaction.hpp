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

    static void restore(T& from, T& to)
    {
	std::swap(from, to);
    }
};

template <typename T, typename Traits = DefaultTransTraits<T> >
class Transaction 
{
public:
    Transaction(T& obj);
    ~Transaction();

    void commit();
    bool rollback();

private:
    Transaction(const Transaction&);
    Transaction& operator = (const Transaction&);

    void doRollback();
    void createBackup();

    bool m_committed;
    T* m_pObj;
    T& m_obj;
};

template <typename T, typename Traits>
Transaction<T, Traits>::Transaction(T& obj)
    : m_committed(false), m_pObj(NULL), m_obj(obj)
{
    m_pObj = new T();
    createBackup();
}


template <typename T, typename Traits>
Transaction<T, Traits>::~Transaction()
{
    if (!m_pObj) return;
    if (!m_committed)
    {
        try
        {
	    doRollback();
        }
        catch (...)
        {
        }
    }
    delete m_pObj;
}

template <typename T, typename Traits>
void Transaction<T, Traits>::commit()
{
    m_committed = true;
}


template <typename T, typename Traits>
bool Transaction<T, Traits>::rollback()
{
    if (!m_committed && m_pObj)
    {
	doRollback();
        createBackup();
	return true;
    }
    return false;
}

template <typename T, typename Traits>
void Transaction<T, Traits>::doRollback()
{
    Traits::restore(*m_pObj, m_obj);
}

template <typename T, typename Traits>
void Transaction<T, Traits>::createBackup()
{
    Traits::backup(m_obj, *m_pObj);
}

}

#endif
