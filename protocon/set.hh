
#ifndef Set_HH_
#define Set_HH_

#include <set>

template <class T>
class Set : public std::set<T>
{
public:
  bool elemCk(const T& e) const
  {
    return (this->find(e) != this->end());
  }

  Set<T>& operator|=(const T& e)
  { insert(e); return *this; }

  Set<T>& operator|=(const Set<T>& b)
  {
    insert(b.begin(), b.end());
    return *this;
  }

  Set<T> operator|(const Set<T>& b) const
  {
    Set c( *this );
    return c |= b;
  }

  Set<T>& operator-=(const Set<T>& b)
  {
    Set<T>& a = *this;
    typename Set<T>::const_iterator itb = b.begin();
    typename Set<T>::iterator ita = a.lower_bound(*itb);
    typename Set<T>::key_compare f = a.key_comp();

    while (ita != a.end() && itb != b.end()) {
      if (f(*ita,*itb)) {
        ita = b.lower_bound(*ita);
      }
      else if (f(*itb,*ita)) {
        itb = b.lower_bound(*ita);
      }
      else {
        typename Set<T>::iterator tmp = ita;
        ++ita;
        ++itb;
        a.erase(tmp);
      }
    }
    return a;
  }

  Set<T> operator-(const Set<T>& b) const
  {
    Set<T> c( *this );
    c -= b;
    return c;
  }

  Set<T>& operator&=(const Set<T>& b) const
  {
    return (*this -= (*this - b));
  }

  Set<T> operator&(const Set<T>& b) const
  {
    if (this->size() < b.size()) {
      return (*this - (*this - b));
    }
    return (b - (b - *this));
  }
};

#endif
