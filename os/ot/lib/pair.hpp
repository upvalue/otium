#ifndef OT_SHARED_PAIR_HPP
#define OT_SHARED_PAIR_HPP

/**
 * Pair - A simple pair container (STL-free alternative to std::pair)
 *
 * Template parameters T1 and T2 are the types of the two elements.
 */
template <typename T1, typename T2> struct Pair {
  T1 first;
  T2 second;

  // Default constructor
  Pair() : first(), second() {}

  // Value constructor
  Pair(const T1 &f, const T2 &s) : first(f), second(s) {}
};

/**
 * make_pair - Helper function to create a Pair with type deduction
 */
template <typename T1, typename T2>
Pair<T1, T2> make_pair(const T1 &first, const T2 &second) {
  return Pair<T1, T2>(first, second);
}

#endif
