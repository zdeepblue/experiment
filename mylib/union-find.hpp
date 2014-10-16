#ifndef HQW_UNION_FIND_HPP
#define HQW_UNION_FIND_HPP


namespace hqw {

template <unsigned int SZ>
class UnionFind
{
  private:
    unsigned int m_id[SZ];
    unsigned int m_sz[SZ];

    unsigned int root(unsigned int i)
    {
      unsigned int r = i;
      while ( r != m_id[r])
      {
        r = m_id[r];
      }
      while ( i != m_id[i])
      {
        unsigned int j = m_id[i];
        m_id[i] = r;
        i = j
      }
      return r;
    }
  public:
    UnionFind()
    {
      for (unsigned int i = 0; i < SZ; ++i)
      {
        m_id[i] = i;
        m_sz[i] = 1;
      }
    }

    bool union2(unsigned int i, unsigned int j)
    {
      if (i >= SZ || j >= SZ) return false;
      unsigned int iRoot = root(i);
      unsigned int jRoot = root(j);
      if (iRoot == jRoot) return false;
      if (m_sz[iRoot] < m_sz[jRoot])
      {
        m_id[iRoot] = jRoot;
        m_sz[jRoot] += m_sz[iRoot];
      }
      else
      {
        m_id[jRoot] = iRoot;
        m_sz[iRoot] += m_sz[jRoot];
      }

      return true;
    }

    bool find(unsigned int i, unsigned int j)
    {
      return root(i) == root(j);
    }

};

}

#endif
