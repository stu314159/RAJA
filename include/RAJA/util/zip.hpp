/*!
 ******************************************************************************
 *
 * \file
 *
 * \brief   Header file for multi-iterator Zip Views.
 *
 ******************************************************************************
 */

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-20, Lawrence Livermore National Security, LLC
// and RAJA project contributors. See the RAJA/COPYRIGHT file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//


#ifndef RAJA_util_zip_HPP
#define RAJA_util_zip_HPP

#include "RAJA/config.hpp"

#include <iostream>
#include <type_traits>

#include "RAJA/pattern/detail/algorithm.hpp"
#include "RAJA/util/camp_aliases.hpp"
#include "RAJA/util/concepts.hpp"
#include "RAJA/util/zip_tuple.hpp"

namespace RAJA
{

template < typename... Iters >
struct ZipIterator
{
  static_assert(concepts::all_of<type_traits::is_random_access_iterator<Iters>...>::value,
      "ZipIterator can only contain random access iterators");
  static_assert(sizeof...(Iters) > 1,
      "ZipIterator must contain one or more iterators");

  using value_type = detail::zip_val<typename std::iterator_traits<Iters>::value_type...>;
  using difference_type = std::ptrdiff_t;
  using pointer = void;
  using reference = detail::zip_ref<typename std::iterator_traits<Iters>::reference...>;
  using creference = detail::zip_ref<const typename std::iterator_traits<Iters>::reference...>;
  using iterator_category = std::random_access_iterator_tag;

  RAJA_HOST_DEVICE inline ZipIterator()
    : m_iterators()
  {
  }

  template < typename... Args,
             typename = concepts::enable_if<DefineConcept(concepts::convertible_to<Iters>(camp::val<Args&&>()))...> >
  RAJA_HOST_DEVICE inline ZipIterator(Args&&... args)
    : m_iterators(std::forward<Args>(args)...)
  {
  }

  RAJA_HOST_DEVICE inline ZipIterator(const ZipIterator& rhs)
    : m_iterators(rhs.m_iterators)
  {
  }
  RAJA_HOST_DEVICE inline ZipIterator(ZipIterator&& rhs)
    : m_iterators(std::move(rhs.m_iterators))
  {
  }

  RAJA_HOST_DEVICE inline ZipIterator& operator=(const ZipIterator& rhs)
  {
    m_iterators = rhs.m_iterators;
    return *this;
  }
  RAJA_HOST_DEVICE inline ZipIterator& operator=(ZipIterator&& rhs)
  {
    m_iterators = std::move(rhs.m_iterators);
    return *this;
  }


  RAJA_HOST_DEVICE inline difference_type get_stride() const { return 1; }

  RAJA_HOST_DEVICE inline bool operator==(const ZipIterator& rhs) const
  {
    return m_iterators.template get<0>() == rhs.m_iterators.template get<0>();
  }
  RAJA_HOST_DEVICE inline bool operator!=(const ZipIterator& rhs) const
  {
    return m_iterators.template get<0>() != rhs.m_iterators.template get<0>();
  }
  RAJA_HOST_DEVICE inline bool operator>(const ZipIterator& rhs) const
  {
    return m_iterators.template get<0>() > rhs.m_iterators.template get<0>();
  }
  RAJA_HOST_DEVICE inline bool operator<(const ZipIterator& rhs) const
  {
    return m_iterators.template get<0>() < rhs.m_iterators.template get<0>();
  }
  RAJA_HOST_DEVICE inline bool operator>=(const ZipIterator& rhs) const
  {
    return m_iterators.template get<0>() >= rhs.m_iterators.template get<0>();
  }
  RAJA_HOST_DEVICE inline bool operator<=(const ZipIterator& rhs) const
  {
    return m_iterators.template get<0>() <= rhs.m_iterators.template get<0>();
  }

  RAJA_HOST_DEVICE inline ZipIterator& operator++()
  {
    detail::zip_for_each(m_iterators, detail::PreInc{});
    return *this;
  }
  RAJA_HOST_DEVICE inline ZipIterator& operator--()
  {
    detail::zip_for_each(m_iterators, detail::PreDec{});
    return *this;
  }
  RAJA_HOST_DEVICE inline ZipIterator operator++(int)
  {
    ZipIterator tmp(*this);
    ++(*this);
    return tmp;
  }
  RAJA_HOST_DEVICE inline ZipIterator operator--(int)
  {
    ZipIterator tmp(*this);
    --(*this);
    return tmp;
  }

  RAJA_HOST_DEVICE inline ZipIterator& operator+=(
      const difference_type& rhs)
  {
    detail::zip_for_each(m_iterators, detail::PlusEq<difference_type>{rhs});
    return *this;
  }
  RAJA_HOST_DEVICE inline ZipIterator& operator-=(
      const difference_type& rhs)
  {
    detail::zip_for_each(m_iterators, detail::MinusEq<difference_type>{rhs});
    return *this;
  }

  RAJA_HOST_DEVICE inline difference_type operator-(
      const ZipIterator& rhs) const
  {
    return m_iterators.template get<0>() - rhs.m_iterators.template get<0>();
  }
  RAJA_HOST_DEVICE inline ZipIterator operator+(
      const difference_type& rhs) const
  {
    ZipIterator tmp(*this);
    tmp += rhs;
    return tmp;
  }
  RAJA_HOST_DEVICE inline ZipIterator operator-(
      const difference_type& rhs) const
  {
    ZipIterator tmp(*this);
    tmp -= rhs;
    return tmp;
  }
  RAJA_HOST_DEVICE friend ZipIterator operator+(
      difference_type lhs,
      const ZipIterator& rhs)
  {
    ZipIterator tmp(rhs);
    tmp += lhs;
    return tmp;
  }

  RAJA_HOST_DEVICE inline reference operator*() const
  {
    return deref_helper(camp::make_idx_seq_t<sizeof...(Iters)>{});
  }
  // TODO:: figure out what to do with this
  // RAJA_HOST_DEVICE inline reference operator->() const
  // {
  //   return *(*this);
  // }
  RAJA_HOST_DEVICE reference operator[](difference_type rhs) const
  {
    return *((*this) + rhs);
  }


  RAJA_HOST_DEVICE friend inline void safe_iter_swap(ZipIterator& lhs, ZipIterator& rhs)
  {
    detail::zip_for_each(lhs.m_iterators, rhs.m_iterators, detail::IterSwap{});
  }

private:
  detail::zip_val<camp::decay<Iters>...> m_iterators;

  template < camp::idx_t ... Is >
  RAJA_HOST_DEVICE inline reference deref_helper(camp::idx_seq<Is...>) const
  {
    return reference(*m_iterators.template get<Is>()...);
  }
};


template < typename... Args >
RAJA_HOST_DEVICE
auto zip(Args&&... args)
  -> ZipIterator<camp::decay<Args>...>
{
  return {std::forward<Args>(args)...};
}

template < typename T, typename Compare >
struct CompareFirst
{
  RAJA_HOST_DEVICE inline CompareFirst(Compare comp_)
    : comp(comp_)
  { }

  RAJA_HOST_DEVICE inline bool operator()(T const& lhs, T const& rhs)
  {
    return comp(lhs.template get<0>(), rhs.template get<0>());
  }

private:
  Compare comp;
};

template < typename T, typename Compare >
RAJA_HOST_DEVICE
auto compare_first(Compare comp)
  -> CompareFirst<T, Compare>
{
  return {comp};
}

}  // end namespace RAJA

#endif
