/**
 * @defgroup PrimeTable Prime Number Table
 * Routines to enumerate prime numbers using the sieve of Eratosthenes.
 *
 * @ingroup Library
 *
 * This module provides routines to enumerate prime numbers below a given
 * threshold. The threshold can be expressed as either the largest prime
 * to find or the number of primes to find.
 *
 * The sieve of Eratosthenes method is used to enumerate the prime numbers.
 * This has a few implications.
 * First, the primes are enumerated all at once
 * at the beginning and stored in memory. The space complexity is therefore
 * in proportion to the largest prime enumerated.
 * Second, the primes generated
 * always start from the smallest prime (i.e. @c 2).
 * Lastly, primality test of
 * a number smaller than the largest prime enumerated is straightforward and
 * takes constant time.
 *
 * Segmented Sieve of Eratosthenes.
 *
 * The segmented sieve method implemented here is an improvement to the
 * ordinary Sieve of Eratosthenes. Instead of sieving the whole range at
 * once, it divides the range into smaller segments where each fits in 
 * the CPU cache. This leads to a significant performance improvement
 * over the standard method for large range.
 *
 * Further performance improvements can be achieved by introducing more
 * advanced optimizations, including wheel factorization, multithreading,
 * etc. A good summary as well as implementation can be found at
 *   - http://code.google.com/p/primesieve/
 *
 * Therefore, this implementation should be used as a demonstration of 
 * concept rather than a high performance reference. The algorithm is
 * outlined below.
 *
 * We use a bit-array to store the sieving status for odd numbers.
 * An odd number <code>n = 2k + 1</code> is mapped to index @c k in
 * the array. The first element in the array, corresponding to 
 * <code>k = 0</code> and <code>n = 1</code>, is ignored.
 * 
 * Given an upper bound <code>N</code>, we first sieve all primes less
 * than or equal to <code>sqrt(N)</code> using the ordinary Sieve of
 * Eratosthenes.
 * 
 * We then divide the remaining candidate odd numbers <code>sqrt(N)
 * <= n <= N</code> into segments where each segment contains 
 * <code>window</code> numbers. 
 *
 */

#ifndef EULER_PRIME_TABLE_HPP
#define EULER_PRIME_TABLE_HPP

#include <vector>
#include <iterator>
#include <cassert>
#include <algorithm>
#include <utility>
#include <cmath>
#include "dynamic_bitset.hpp"
#include "imath.hpp"

///////////////////////////////////////////////////////////////////////////
// Routines to Enumerate Prime Numbers.

namespace euler {

/**
 * Returns the bounds of the <code>n</code>-th prime. 
 *
 * Let <code>p<sub>n</sub></code> denote the <code>n</code>-th prime.
 * For <code>n > 6</code>, the following inequality is used to estimate 
 * the bounds:
 * \f[
 * n \ln n + n \ln \ln n - n < p_n < n \ln n + n \ln \ln n .
 * \f]
 * More information can be found at
 *    - http://en.wikipedia.org/wiki/Prime-counting_function
 *
 * @param n The <code>n</code>-th prime whose bounds are estimated.
 * @returns The bounds (both inclusive) stored in a <code>std::pair</code>,
 *      where @c first contains the lower bound, and @c second contains 
 *      the upper bound.
 * @complexity Constant.
 * 
 * @ingroup PrimeTable
 */
template <typename T>
std::pair<T,T> nth_prime_bounds(T n)
{
  if (n < 6)
  {
    return std::pair<T,T>(2, 11);
  }
  else
  {
    long double ln_n = std::log(static_cast<long double>(n));
    long double ln_ln_n = log(ln_n);
    long double t = ln_n + ln_ln_n;
    return std::pair<T,T>(static_cast<T>(n*t)-n-1, static_cast<T>(n*t)+1);
  }
}

template <class T> class prime_table;
template <class T> class prime_iterator;
template <class T> class prime_reverse_iterator;

#ifndef USE_SEGMENTED_SIEVE
#define USE_SEGMENTED_SIEVE 1
#endif

/**
 * Prime number table generated by the sieve of Eratosthenes.
 *
 * @ingroup PrimeTable
 */
template <typename T>
class prime_table
{
  T _limit;
  dynamic_bitset<> table;

  void mark_non_prime(T n) 
  { 
    table.reset(n/2);
  }

public:

  /**
   * Constructs a prime number table that stores all the primes not larger
   * than a given limit.
   *
   * @param N Limit of the prime number table. Must be positive. All integers
   *    less than or equal to @c N are tested for primality and the result
   *    stored.
   *
   * @timecomplexity <code>O(N log log N)</code>.
   *
   * @spacecomplexity Object size is <code>O(N)</code>.
   */
  explicit prime_table(T N) : _limit(N), table((N+1)/2, true)
  {
    assert(N >= 0);

#if USE_SEGMENTED_SIEVE
    // Use a bit-array to store whether each odd number is prime.
    // 1 -> 0, 3 -> 1, 5 -> 2, ..., odd n -> (n-1)/2.
    // table[k] is true <=> (2k+1) is prime.
    T K = (N-1)/2;

    // Mark 1 as non-prime.
    table.reset(0);

    // First sieve small primes up to sqrt(N) using ordinary method.
    // This is equivalent to sieving primes (2k+1) up to SK (small_k).
    // SK is such that (2*SK+1)^2 <= N < [2*(SK+1)+1]^2.
    T SMALL_N = euler::isqrt(N);
    T SMALL_K = (SMALL_N-1)/2;
    T estimated_small_count = static_cast<T>(SMALL_N / std::log(SMALL_N));

    // Store each small prime.
    std::vector<T> prime_p;
    prime_p.reserve(estimated_small_count);

    // Store the next odd multiple of each prime that should be crossed out.
    std::vector<T> next_multiple_t;
    next_multiple_t.reserve(estimated_small_count);

    // Sieve small primes.
    for (T k = 1; k <= SMALL_K; k++)
    {
      if (table.test(k)) // (2k+1) is prime
      {
        T p = k*2+1;
        T t = 2*k*(k+1); // 2t+1 = p^2
        for (; t <= SMALL_K; t += p)
        {
          table.reset(t);
        }
        prime_p.push_back(p);
        next_multiple_t.push_back(t);
      }
    }
  
    // Use the small prime table to sieve the whole range.
    T window = 32*1000*16/2; // 16 numbers per byte, ~ 32k window
    for (T segment_start = SMALL_K + 1; segment_start <= K; segment_start += window)
    {
      T segment_end = std::min(segment_start+window, K+1);
      for (size_t i = 0; i < prime_p.size(); ++i)
      {
        T t = next_multiple_t[i];
        if (t < segment_end)
        {
          T p = prime_p[i];
          for (; t < segment_end; t += p)
          {
            table.reset(t);
          }
          next_multiple_t[i] = t;
        }
      }
    }
#else
    // Each odd number n is mapped to (n/2) in the bitset.
    // 1 => 0, 3 => 1, 5 => 2, etc.
    mark_non_prime(1);
    for (T p = 3; p*p <= limit; p += 2)
    {
      if (test_odd(p)) // is prime
      {
        T t = p*p;
        do
        {
          mark_non_prime(t);
        }
        while ((t += 2*p) <= limit);
      }
    }
#endif
  }

#if 0
  /// Tests whether the prime number table contains no primes.
  /// @return @c true if there are no primes in the table, @c false otherwise.
  bool empty() const   {
    return _max < 2;
  }
#endif

  /// Returns the limit of the prime table.
  T limit() const { return _limit; }

  /**
   * Tests whether an odd integer, @c n, is prime by looking up the prime
   * table. @c n must be smaller than or equal to the limit of the table.
   * @param n An odd integer whose primality is checked.
   * @return @c true if @c n is prime, @c false if @c n is composite.
   * @timecomplexity Constant.
   * @spacecomplexity Constant.
   */
  bool test_odd(T n) const
  {
    assert(n > 0 && n % 2 != 0);
    return table[n/2]; 
  }

  /**
   * Tests whether an integer, @c n, is prime by looking up the prime table.
   * @c n must be smaller than or equal to the limit of the table.
   * @param n The number whose primality is to be tested.
   * @return @c true if @c n is prime, @c false if @c n is composite.
   * @timecomplexity Constant.
   * @spacecomplexity Constant.
   */
  bool test(T n) const
  {
    assert(n >= 1 && n <= _limit);
    if (n == 1)
    {
      return false;
    }
    else if (n == 2)
    {
      return true;
    }
    else if (n % 2 == 0)
    {
      return false;
    }
    else
    {
      return test_odd(n);
    }
  }

  /// Gets an iterator that points to the smallest prime in the table.
  prime_iterator<T> begin() const { return prime_iterator<T>(*this, 2); }

  /// Gets an iterator that points past the largest prime in the table.
  prime_iterator<T> end() const   { return prime_iterator<T>(*this, 0); }

  /**
   * Finds the smallest prime in the table that is greater than or equal 
   * to a given number.
   * @param n The lower bound.
   * @returns An iterator that points to the smallest prime in the table
   *      that is greater than or equal to @c n. If such a prime is not
   *      found, returns <code>end()</code>.
   * @timecomplexity 
   * @spacecomplexity Constant.
   */
  prime_iterator<T> lower_bound(T n) const
  {
    prime_iterator<T> it = begin();
    while (it != end() && *it < n)
    {
      ++it;
    }
    return it;
  }

#if 0
  /// Gets a reverse iterator that points to the largest prime in the table.
  const_reverse_iterator crbegin() const
  {
    int p = (_max % 2 == 0)? _max - 1 : _max;
    for (; p >= 3; p -= 2)
    {
      if (_table[p/2])
        break;
    }
    if (p < 3)
    {
      p = (_max >= 2)? 2 : 0;
    }
    return const_reverse_iterator(*this, p);
  }

  /// Gets a reverse iterator that points before the smallest prime in the table.
  const_reverse_iterator crend() const
  {
    return const_reverse_iterator(*this, 0);
  }

  /// Gets an iterator that points to the smallest prime in the table.
  iterator begin() const { return cbegin(); }

  /// Gets an iterator that points past the largest prime in the table.
  iterator end() const { return cend(); }

  /// Gets a reverse iterator that points to the largest prime in the table.
  reverse_iterator rbegin() const { return crbegin(); }

  /// Gets a reverse iterator that points before the smallest prime in the table.
  reverse_iterator rend() const { return crend(); }

  /// Returns the number of prime numbers in the table.
  size_type size() const
  {
    return (size_type)std::count(_table.cbegin(), _table.cend(), true);
  }
#endif
};

/// Iterator used to enumerate the primes from smallest to largest in a 
/// precomputed prime number table.
template <class T>
class prime_iterator : 
  public std::iterator<std::forward_iterator_tag, int, std::ptrdiff_t, void, T>
{
private:
  const prime_table<T> &_table;
  T _current;

public:
  
  /// Constructs the iterator.
  prime_iterator(const prime_table<T> &table, T current)
    : _table(table), _current(current) { }

#if 0
  /// Constructs the iterator.
  prime_iterator(const prime_table<T> &table)
    : _table(table), _current(table.limit() >= 2? 2 : 0) { }
#endif

  /// Returns the current prime.
  T operator * () const {   return _current; }

  /// Advances the iterator.
  prime_iterator& operator ++ ()
  {
    assert(_current != 0);

    T p;
    if (_current == 2)
    {
      p = 3;
    }
    else
    {
      for (p = _current + 2; p <= _table.limit(); p += 2)
      {
        if (_table.test_odd(p))
        {
          break;
        }
      }
    }

    _current = (p <= _table.limit())? p : 0;
    return *this;
  }

  /// Tests whether this iterator is equal to another iterator.
  /// For performance reason, two prime iterator are equal if and only if
  /// they are both past-the-end.
  /// @complexity Constant.
  bool operator == (const prime_iterator &it) const
  {
    return (_current == 0) && (it._current == 0);
  }

  /// Tests whether this iterator is not equal to another iterator.
  /// @complexity Constant.
  bool operator != (const prime_iterator &it) const
  {
    return ! operator == (it);
  }
};


#if 0
  /// Iterator used to enumerate the primes from largest to smallest
  /// in a precomputed prime number table.
  class const_reverse_iterator : public std::iterator<std::forward_iterator_tag, int>
  {
  private:
    const prime_numbers &_primes;
    int _current;

  public:
    /// Constructs the iterator.
    const_reverse_iterator(const prime_numbers &primes, int current)
      : _primes(primes), _current(current) { }

    /// Advances the iterator.
    const_reverse_iterator& operator ++ ()
    {
      assert(_current != 0);

      int p;
      if (_current == 2)
      {
        p = 0;
      }
      else if (_current == 3)
      {
        p = 2;
      }
      else
      {
        for (p = _current - 2; p >= 3; p -= 2)
        {
          if (_primes._table[p/2])
            break;
        }
      }

      _current = p;
      return *this;
    }

    /// Returns the current value.
    int operator * () const
    {
      return _current;
    }

    /// Tests whether this iterator is equal to another iterator.
    bool operator == (const const_reverse_iterator &it) const
    {
      return (&_primes == &it._primes && _current == it._current);
    }

    /// Tests whether this iterator is not equal to another iterator.
    bool operator != (const const_reverse_iterator &it) const
    {
      return !(&_primes == &it._primes && _current == it._current);
    }
  };
#endif


} // namespace euler

#endif // EULER_PRIME_TABLE_HPP
