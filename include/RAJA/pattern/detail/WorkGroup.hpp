/*!
 ******************************************************************************
 *
 * \file
 *
 * \brief   Header file providing RAJA WorkPool and WorkGroup declarations.
 *
 ******************************************************************************
 */

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-20, Lawrence Livermore National Security, LLC
// and RAJA project contributors. See the RAJA/COPYRIGHT file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

#ifndef RAJA_PATTERN_DETAIL_WorkGroup_HPP
#define RAJA_PATTERN_DETAIL_WorkGroup_HPP

#include "RAJA/config.hpp"

#include "RAJA/util/Operators.hpp"
#include "RAJA/util/macros.hpp"

#include "RAJA/policy/WorkGroup.hpp"
#include "RAJA/policy/loop/policy.hpp"
#include "RAJA/pattern/forall.hpp"

#include <type_traits>
#include <cstddef>

namespace RAJA
{

namespace detail
{

/*!
 * A simple vector like class
 */
template < typename T, typename Allocator >
struct SimpleVector
{
  SimpleVector(Allocator aloc)
    : m_aloc(std::forward<Allocator>(aloc))
  { }

  SimpleVector(SimpleVector const&) = delete;
  SimpleVector& operator=(SimpleVector const&) = delete;

  SimpleVector(SimpleVector&& o)
    : m_aloc(o.m_aloc)
    , m_begin(o.m_begin)
    , m_end(o.m_end)
    , m_cap(o.m_cap)
  {
    o.m_begin = nullptr;
    o.m_end   = nullptr;
    o.m_cap   = nullptr;
  }

  SimpleVector& operator=(SimpleVector&& o)
  {
    m_aloc  = o.m_aloc;
    m_begin = o.m_begin;
    m_end   = o.m_end  ;
    m_cap   = o.m_cap  ;

    o.m_begin = nullptr;
    o.m_end   = nullptr;
    o.m_cap   = nullptr;
  }

  Allocator const& get_allocator() const
  {
    return m_aloc;
  }

  Allocator& get_allocator()
  {
    return m_aloc;
  }

  size_t size() const
  {
    return m_end - m_begin;
  }

  const T* begin() const
  {
    return m_begin;
  }

  const T* end() const
  {
    return m_end;
  }

  T* begin()
  {
    return m_begin;
  }

  T* end()
  {
    return m_end;
  }

  void reserve(size_t count)
  {
    if (count > size()) {
      T* new_begin = static_cast<T*>(m_aloc.allocate(count*sizeof(T)));
      T* new_end   = new_begin + size();
      T* new_cap   = new_begin + count;

      for (size_t i = 0; i < size(); ++i) {
        new(&new_begin[i]) T(std::move(m_begin[i]));
        m_begin[i].~T();
      }

      m_aloc.deallocate(m_begin);

      m_begin = new_begin;
      m_end   = new_end  ;
      m_cap   = new_cap  ;
    }
  }

  template < typename ... Os >
  void emplace_back(Os&&... os)
  {
    if (m_end == m_cap) {
      reserve((m_begin == m_cap) ? (size_t)1 : 2*size());
    }
    new(m_end++) T(std::forward<Os>(os)...);
  }

  T pop_back()
  {
    --m_end;
    T last = std::move(*m_end);
    m_end->~T();
    return last;
  }

  void clear()
  {
    for (size_t i = 0; i < size(); ++i) {
      m_begin[i].~T();
    }

    m_aloc.deallocate(m_begin);

    m_begin = nullptr;
    m_end   = nullptr;
    m_cap   = nullptr;
  }

  ~SimpleVector()
  {
    clear();
  }

private:
  Allocator m_aloc;
  T* m_begin = nullptr;
  T* m_end   = nullptr;
  T* m_cap   = nullptr;
};


template < typename T, typename ... CallArgs >
void Vtable_move_construct(void* dest, void* src)
{
  T* dest_as_T = static_cast<T*>(dest);
  T* src_as_T = static_cast<T*>(src);
  new(dest_as_T) T(std::move(*src_as_T));
}

template < typename T, typename ... CallArgs >
void Vtable_call(const void* obj, CallArgs... args)
{
  const T* obj_as_T = static_cast<const T*>(obj);
  (*obj_as_T)(std::forward<CallArgs>(args)...);
}

template < typename T, typename ... CallArgs >
void Vtable_destroy(void* obj)
{
  T* obj_as_T = static_cast<T*>(obj);
  (*obj_as_T).~T();
}

/*!
 * A vtable abstraction
 *
 * Provides function pointers for basic functions.
 */
template < typename ... CallArgs >
struct Vtable {
  using move_sig = void(*)(void* /*dest*/, void* /*src*/);
  using call_sig = void(*)(const void* /*obj*/, CallArgs... /*args*/);
  using destroy_sig = void(*)(void* /*obj*/);

  move_sig move_construct;
  call_sig call;
  destroy_sig destroy;
  size_t size;
};

template < typename ... CallArgs >
using Vtable_move_sig = typename Vtable<CallArgs...>::move_sig;
template < typename ... CallArgs >
using Vtable_call_sig = typename Vtable<CallArgs...>::call_sig;
template < typename ... CallArgs >
using Vtable_destroy_sig = typename Vtable<CallArgs...>::destroy_sig;

/*!
 * Populate and return a Vtable object appropriate for the given policy
 */
// template < typename T, typename ... CallArgs >
// inline Vtable<CallArgs...> get_Vtable(work_policy const&);


/*!
 * A struct that gives a generic way to layout memory for different loops
 */
template < size_t size, typename ... CallArgs >
struct WorkStruct;

/*!
 * Generic struct used to layout memory for structs of unknown size.
 * Assumptions for any size (checked in construct):
 *   offsetof(GenericWorkStruct<>, obj) == offsetof(WorkStruct<size>, obj)
 *   sizeof(GenericWorkStruct) <= sizeof(WorkStruct<size>)
 */
template < typename ... CallArgs >
using GenericWorkStruct = WorkStruct<alignof(std::max_align_t), CallArgs...>;

template < size_t size, typename ... CallArgs >
struct WorkStruct
{
  using vtable_type = Vtable<CallArgs...>;

  template < typename holder, typename ... holder_ctor_args >
  static RAJA_INLINE
  void construct(void* ptr, vtable_type* vtable, holder_ctor_args&&... ctor_args)
  {
    using true_value_type = WorkStruct<sizeof(holder), CallArgs...>;
    using value_type = GenericWorkStruct<CallArgs...>;

    static_assert(sizeof(holder) <= sizeof(true_value_type::obj),
        "holder must fit in WorkStruct::obj");
    static_assert(std::is_standard_layout<true_value_type>::value,
        "WorkStruct must be a standard layout type");
    static_assert(std::is_standard_layout<value_type>::value,
        "GenericWorkStruct must be a standard layout type");
    static_assert(offsetof(value_type, obj) == offsetof(true_value_type, obj),
        "WorkStruct and GenericWorkStruct must have obj at the same offset");
    static_assert(sizeof(value_type) <= sizeof(true_value_type),
        "WorkStruct must not be smaller than GenericWorkStruct");

    true_value_type* value_ptr = static_cast<true_value_type*>(ptr);

    value_ptr->vtable = vtable;
    value_ptr->call_function_ptr = vtable->call;
    new(&value_ptr->obj) holder(std::forward<holder_ctor_args>(ctor_args)...);
  }

  static RAJA_INLINE
  void move_destroy(WorkStruct* value_dst,
                    WorkStruct* value_src)
  {
    value_dst->vtable = value_src->vtable;
    value_dst->call_function_ptr = value_src->call_function_ptr;
    value_dst->vtable->move_construct(&value_dst->obj, &value_src->obj);
    value_dst->vtable->destroy(&value_src->obj);
  }

  static RAJA_INLINE
  void destroy(WorkStruct* value_ptr)
  {
    value_ptr->vtable->destroy(&value_ptr->obj);
  }

  static RAJA_HOST_DEVICE RAJA_INLINE
  void call(const WorkStruct* value_ptr, CallArgs... args)
  {
    value_ptr->call_function_ptr(&value_ptr->obj, std::forward<CallArgs>(args)...);
  }

  vtable_type* vtable;
  Vtable_call_sig<CallArgs...> call_function_ptr;
  typename std::aligned_storage<size, alignof(std::max_align_t)>::type obj;
};


/*!
 * A storage container for work groups
 */
template < typename STORAGE_POLICY_T, typename ALLOCATOR_T, typename ... CallArgs >
struct WorkStorage;

template < typename ALLOCATOR_T, typename ... CallArgs >
struct WorkStorage<RAJA::array_of_pointers, ALLOCATOR_T, CallArgs...>
{
  using storage_policy = RAJA::array_of_pointers;
  using Allocator = ALLOCATOR_T;

  template < typename holder >
  using true_value_type = WorkStruct<sizeof(holder), CallArgs...>;
  using value_type = GenericWorkStruct<CallArgs...>;
  using vtable_type = typename value_type::vtable_type;

  struct const_iterator
  {
    using value_type = const typename WorkStorage::value_type;
    using pointer = value_type*;
    using reference = value_type&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;

    const_iterator(value_type* const* ptrptr)
      : m_ptrptr(ptrptr)
    { }

    reference operator*() const
    {
      return **m_ptrptr;
    }

    pointer operator->() const
    {
      return &(*(*this));
    }

    reference operator[](difference_type i) const
    {
      const_iterator copy = *this;
      copy += i;
      return *copy;
    }

    const_iterator& operator++()
    {
      ++m_ptrptr;
      return *this;
    }

    const_iterator operator++(int)
    {
      const_iterator copy = *this;
      ++(*this);
      return copy;
    }

    const_iterator& operator--()
    {
      --m_ptrptr;
      return *this;
    }

    const_iterator operator--(int)
    {
      const_iterator copy = *this;
      --(*this);
      return copy;
    }

    const_iterator& operator+=(difference_type n)
    {
      m_ptrptr += n;
      return *this;
    }

    const_iterator& operator-=(difference_type n)
    {
      m_ptrptr -= n;
      return *this;
    }

    friend inline const_iterator operator+(
        const_iterator const& iter, difference_type n)
    {
      const_iterator copy = iter;
      copy += n;
      return copy;
    }

    friend inline const_iterator operator+(
        difference_type n, const_iterator const& iter)
    {
      const_iterator copy = iter;
      copy += n;
      return copy;
    }

    friend inline const_iterator operator-(
        const_iterator const& iter, difference_type n)
    {
      const_iterator copy = iter;
      copy -= n;
      return copy;
    }

    friend inline difference_type operator-(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_ptrptr - rhs_iter.m_ptrptr;
    }

    friend inline bool operator==(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_ptrptr == rhs_iter.m_ptrptr;
    }

    friend inline bool operator!=(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return !(lhs_iter == rhs_iter);
    }

    friend inline bool operator<(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_ptrptr < rhs_iter.m_ptrptr;
    }

    friend inline bool operator<=(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_ptrptr <= rhs_iter.m_ptrptr;
    }

    friend inline bool operator>(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_ptrptr > rhs_iter.m_ptrptr;
    }

    friend inline bool operator>=(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_ptrptr >= rhs_iter.m_ptrptr;
    }

  private:
    value_type* const* m_ptrptr;
  };

  WorkStorage(Allocator aloc)
    : m_vec(std::forward<Allocator>(aloc))
  { }

  WorkStorage(WorkStorage const&) = delete;
  WorkStorage& operator=(WorkStorage const&) = delete;

  WorkStorage(WorkStorage&& o)
    : m_vec(std::move(o.m_vec))
    , m_storage_size(o.m_storage_size)
  {
    o.m_storage_size = 0;
  }

  WorkStorage& operator=(WorkStorage&& o)
  {
    m_vec = std::move(o.m_vec);
    m_storage_size = o.m_storage_size;

    o.m_storage_size = 0;
  }

  void reserve(size_t num_loops, size_t loop_storage_size)
  {
    RAJA_UNUSED_VAR(loop_storage_size);
    m_vec.reserve(num_loops);
  }

  // number of loops stored
  size_t size() const
  {
    return m_vec.size();
  }

  const_iterator begin() const
  {
    return const_iterator(m_vec.begin());
  }

  const_iterator end() const
  {
    return const_iterator(m_vec.end());
  }

  size_t storage_size() const
  {
    return m_storage_size;
  }

  template < typename holder, typename ... holder_ctor_args >
  void emplace(Vtable<CallArgs...>* vtable, holder_ctor_args&&... ctor_args)
  {
    m_vec.emplace_back(create_value<holder>(
        vtable, std::forward<holder_ctor_args>(ctor_args)...));
  }

  ~WorkStorage()
  {
    for (size_t count = m_vec.size(); count > 0; --count) {
      destroy_value(m_vec.pop_back());
    }
  }

private:
  SimpleVector<value_type*, Allocator> m_vec;
  size_t m_storage_size = 0;

  template < typename holder, typename ... holder_ctor_args >
  value_type* create_value(Vtable<CallArgs...>* vtable,
                           holder_ctor_args&&... ctor_args)
  {
    value_type* value_ptr = static_cast<value_type*>(
        m_vec.get_allocator().allocate(sizeof(true_value_type<holder>)));
    m_storage_size += sizeof(true_value_type<holder>);

    value_type::template construct<holder>(
        value_ptr, vtable, std::forward<holder_ctor_args>(ctor_args)...);

    return value_ptr;
  }

  void destroy_value(value_type* value_ptr)
  {
    value_type::destroy(value_ptr);
    m_vec.get_allocator().deallocate(value_ptr);
  }
};

template < typename ALLOCATOR_T, typename ... CallArgs >
struct WorkStorage<RAJA::ragged_array_of_objects, ALLOCATOR_T, CallArgs...>
{
  using storage_policy = RAJA::ragged_array_of_objects;
  using Allocator = ALLOCATOR_T;

  template < typename holder >
  using true_value_type = WorkStruct<sizeof(holder), CallArgs...>;
  using value_type = GenericWorkStruct<CallArgs...>;
  using vtable_type = typename value_type::vtable_type;

  struct const_iterator
  {
    using value_type = const typename WorkStorage::value_type;
    using pointer = value_type*;
    using reference = value_type&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;

    const_iterator(const char* array_begin, const size_t* offset_iter)
      : m_array_begin(array_begin)
      , m_offset_iter(offset_iter)
    { }

    reference operator*() const
    {
      return *reinterpret_cast<const value_type*>(
          m_array_begin + *m_offset_iter);
    }

    pointer operator->() const
    {
      return &(*(*this));
    }

    reference operator[](difference_type i) const
    {
      const_iterator copy = *this;
      copy += i;
      return *copy;
    }

    const_iterator& operator++()
    {
      ++m_offset_iter;
      return *this;
    }

    const_iterator operator++(int)
    {
      const_iterator copy = *this;
      ++(*this);
      return copy;
    }

    const_iterator& operator--()
    {
      --m_offset_iter;
      return *this;
    }

    const_iterator operator--(int)
    {
      const_iterator copy = *this;
      --(*this);
      return copy;
    }

    const_iterator& operator+=(difference_type n)
    {
      m_offset_iter += n;
      return *this;
    }

    const_iterator& operator-=(difference_type n)
    {
      m_offset_iter -= n;
      return *this;
    }

    friend inline const_iterator operator+(
        const_iterator const& iter, difference_type n)
    {
      const_iterator copy = iter;
      copy += n;
      return copy;
    }

    friend inline const_iterator operator+(
        difference_type n, const_iterator const& iter)
    {
      const_iterator copy = iter;
      copy += n;
      return copy;
    }

    friend inline const_iterator operator-(
        const_iterator const& iter, difference_type n)
    {
      const_iterator copy = iter;
      copy -= n;
      return copy;
    }

    friend inline difference_type operator-(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_offset_iter - rhs_iter.m_offset_iter;
    }

    friend inline bool operator==(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_offset_iter == rhs_iter.m_offset_iter;
    }

    friend inline bool operator!=(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return !(lhs_iter == rhs_iter);
    }

    friend inline bool operator<(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_offset_iter < rhs_iter.m_offset_iter;
    }

    friend inline bool operator<=(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_offset_iter <= rhs_iter.m_offset_iter;
    }

    friend inline bool operator>(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_offset_iter > rhs_iter.m_offset_iter;
    }

    friend inline bool operator>=(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_offset_iter >= rhs_iter.m_offset_iter;
    }

  private:
    const char* m_array_begin;
    const size_t* m_offset_iter;
  };


  WorkStorage(Allocator aloc)
    : m_offsets(std::forward<Allocator>(aloc))
  { }

  WorkStorage(WorkStorage const&) = delete;
  WorkStorage& operator=(WorkStorage const&) = delete;

  WorkStorage(WorkStorage&& o)
    : m_offsets(std::move(o.m_offsets))
    , m_array_begin(o.m_array_begin)
    , m_array_end(o.m_array_end)
    , m_array_cap(o.m_array_cap)
  {
    o.m_array_begin = nullptr;
    o.m_array_end = nullptr;
    o.m_array_cap = nullptr;
  }

  WorkStorage& operator=(WorkStorage&& o)
  {
    m_offsets     = std::move(o.m_offsets);
    m_array_begin = o.m_array_begin;
    m_array_end   = o.m_array_end  ;
    m_array_cap   = o.m_array_cap  ;

    o.m_array_begin = nullptr;
    o.m_array_end   = nullptr;
    o.m_array_cap   = nullptr;
  }


  void reserve(size_t num_loops, size_t loop_storage_size)
  {
    m_offsets.reserve(num_loops);
    array_reserve(loop_storage_size);
  }

  // number of loops stored
  size_t size() const
  {
    return m_offsets.size();
  }

  const_iterator begin() const
  {
    return const_iterator(m_array_begin, m_offsets.begin());
  }

  const_iterator end() const
  {
    return const_iterator(m_array_begin, m_offsets.end());
  }

  // amount of storage used to store loops
  size_t storage_size() const
  {
    return m_array_end - m_array_begin;
  }

  template < typename holder, typename ... holder_ctor_args >
  void emplace(Vtable<CallArgs...>* vtable, holder_ctor_args&&... ctor_args)
  {
    m_offsets.emplace_back(create_value<holder>(
        vtable, std::forward<holder_ctor_args>(ctor_args)...));
  }

  ~WorkStorage()
  {
    for (size_t count = size(); count > 0; --count) {
      destroy_value(m_offsets.pop_back());
    }
    if (m_array_begin != nullptr) {
      m_offsets.get_allocator().deallocate(m_array_begin);
    }
  }

private:
  SimpleVector<size_t, Allocator> m_offsets;
  char* m_array_begin = nullptr;
  char* m_array_end   = nullptr;
  char* m_array_cap   = nullptr;

  size_t storage_capacity() const
  {
    return m_array_cap - m_array_begin;
  }

  size_t storage_unused() const
  {
    return m_array_cap - m_array_end;
  }

  void array_reserve(size_t loop_storage_size)
  {
    if (loop_storage_size > storage_capacity()) {

      char* new_array_begin = static_cast<char*>(
          m_offsets.get_allocator().allocate(loop_storage_size));
      char* new_array_end   = new_array_begin + storage_size();
      char* new_array_cap   = new_array_begin + loop_storage_size;

      for (size_t i = 0; i < size(); ++i) {
        value_type* old_value = reinterpret_cast<value_type*>(
            m_array_begin + m_offsets.begin()[i]);
        value_type* new_value = reinterpret_cast<value_type*>(
            new_array_begin + m_offsets.begin()[i]);

        value_type::move_destroy(new_value, old_value);
      }

      m_offsets.get_allocator().deallocate(m_array_begin);

      m_array_begin = new_array_begin;
      m_array_end   = new_array_end  ;
      m_array_cap   = new_array_cap  ;
    }
  }

  template < typename holder, typename ... holder_ctor_args >
  size_t create_value(Vtable<CallArgs...>* vtable,
                      holder_ctor_args&&... ctor_args)
  {
    const size_t value_size = sizeof(true_value_type<holder>);

    if (value_size > storage_unused()) {
      array_reserve(std::max(storage_size() + value_size, 2*storage_capacity()));
    }

    size_t value_offset = storage_size();
    value_type* value_ptr =
        reinterpret_cast<value_type*>(m_array_begin + value_offset);
    m_array_end += value_size;

    value_type::template construct<holder>(
        value_ptr, vtable, std::forward<holder_ctor_args>(ctor_args)...);

    return value_offset;
  }

  void destroy_value(size_t value_offset)
  {
    value_type* value_ptr =
        reinterpret_cast<value_type*>(m_array_begin + value_offset);
    value_type::destroy(value_ptr);
  }
};

template < typename ALLOCATOR_T, typename ... CallArgs >
struct WorkStorage<RAJA::constant_stride_array_of_objects,
                   ALLOCATOR_T,
                   CallArgs...>
{
  using storage_policy = RAJA::constant_stride_array_of_objects;
  using Allocator = ALLOCATOR_T;

  template < typename holder >
  using true_value_type = WorkStruct<sizeof(holder), CallArgs...>;
  using value_type = GenericWorkStruct<CallArgs...>;
  using vtable_type = typename value_type::vtable_type;

  struct const_iterator
  {
    using value_type = const typename WorkStorage::value_type;
    using pointer = value_type*;
    using reference = value_type&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;

    const_iterator(const char* array_pos, size_t stride)
      : m_array_pos(array_pos)
      , m_stride(stride)
    { }

    reference operator*() const
    {
      return *reinterpret_cast<const value_type*>(m_array_pos);
    }

    pointer operator->() const
    {
      return &(*(*this));
    }

    reference operator[](difference_type i) const
    {
      const_iterator copy = *this;
      copy += i;
      return *copy;
    }

    const_iterator& operator++()
    {
      m_array_pos += m_stride;
      return *this;
    }

    const_iterator operator++(int)
    {
      const_iterator copy = *this;
      ++(*this);
      return copy;
    }

    const_iterator& operator--()
    {
      m_array_pos -= m_stride;
      return *this;
    }

    const_iterator operator--(int)
    {
      const_iterator copy = *this;
      --(*this);
      return copy;
    }

    const_iterator& operator+=(difference_type n)
    {
      m_array_pos += n * m_stride;
      return *this;
    }

    const_iterator& operator-=(difference_type n)
    {
      m_array_pos -= n * m_stride;
      return *this;
    }

    friend inline const_iterator operator+(
        const_iterator const& iter, difference_type n)
    {
      const_iterator copy = iter;
      copy += n;
      return copy;
    }

    friend inline const_iterator operator+(
        difference_type n, const_iterator const& iter)
    {
      const_iterator copy = iter;
      copy += n;
      return copy;
    }

    friend inline const_iterator operator-(
        const_iterator const& iter, difference_type n)
    {
      const_iterator copy = iter;
      copy -= n;
      return copy;
    }

    friend inline difference_type operator-(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return (lhs_iter.m_array_pos - rhs_iter.m_array_pos) / lhs_iter.m_stride;
    }

    friend inline bool operator==(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_array_pos == rhs_iter.m_array_pos;
    }

    friend inline bool operator!=(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return !(lhs_iter == rhs_iter);
    }

    friend inline bool operator<(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_array_pos < rhs_iter.m_array_pos;
    }

    friend inline bool operator<=(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_array_pos <= rhs_iter.m_array_pos;
    }

    friend inline bool operator>(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_array_pos > rhs_iter.m_array_pos;
    }

    friend inline bool operator>=(
        const_iterator const& lhs_iter, const_iterator const& rhs_iter)
    {
      return lhs_iter.m_array_pos >= rhs_iter.m_array_pos;
    }

  private:
    const char* m_array_pos;
    size_t m_stride;
  };


  WorkStorage(Allocator aloc)
    : m_aloc(std::forward<Allocator>(aloc))
  { }

  WorkStorage(WorkStorage const&) = delete;
  WorkStorage& operator=(WorkStorage const&) = delete;

  WorkStorage(WorkStorage&& o)
    : m_aloc(o.m_aloc)
    , m_stride(o.m_stride)
    , m_array_begin(o.m_array_begin)
    , m_array_end(o.m_array_end)
    , m_array_cap(o.m_array_cap)
  {
    // do not reset stride, leave it for reuse
    o.m_array_begin = nullptr;
    o.m_array_end   = nullptr;
    o.m_array_cap   = nullptr;
  }

  WorkStorage& operator=(WorkStorage&& o)
  {
    m_aloc        = o.m_aloc       ;
    m_stride      = o.m_stride     ;
    m_array_begin = o.m_array_begin;
    m_array_end   = o.m_array_end  ;
    m_array_cap   = o.m_array_cap  ;

    // do not reset stride, leave it for reuse
    o.m_array_begin = nullptr;
    o.m_array_end   = nullptr;
    o.m_array_cap   = nullptr;
  }

  void reserve(size_t num_loops, size_t loop_storage_size)
  {
    RAJA_UNUSED_VAR(num_loops);
    array_reserve(loop_storage_size, m_stride);
  }

  // number of loops stored
  size_t size() const
  {
    return storage_size() / m_stride;
  }

  const_iterator begin() const
  {
    return const_iterator(m_array_begin, m_stride);
  }

  const_iterator end() const
  {
    return const_iterator(m_array_end,   m_stride);
  }

  // amount of storage used to store loops
  size_t storage_size() const
  {
    return m_array_end - m_array_begin;
  }

  template < typename holder, typename ... holder_ctor_args >
  void emplace(Vtable<CallArgs...>* vtable, holder_ctor_args&&... ctor_args)
  {
    create_value<holder>(vtable, std::forward<holder_ctor_args>(ctor_args)...);
  }

  ~WorkStorage()
  {
    for (size_t value_offset = storage_size(); value_offset > 0; value_offset -= m_stride) {
      destroy_value(value_offset - m_stride);
    }
    if (m_array_begin != nullptr) {
      m_aloc.deallocate(m_array_begin);
    }
  }

private:
  Allocator m_aloc;
  size_t m_stride     = 1; // can't be 0 because size divides stride
  char* m_array_begin = nullptr;
  char* m_array_end   = nullptr;
  char* m_array_cap   = nullptr;

  size_t storage_capacity() const
  {
    return m_array_cap - m_array_begin;
  }

  size_t storage_unused() const
  {
    return m_array_cap - m_array_end;
  }

  void array_reserve(size_t loop_storage_size, size_t new_stride)
  {
    if (loop_storage_size > storage_capacity() || new_stride > m_stride) {

      char* new_array_begin = static_cast<char*>(
          m_aloc.allocate(loop_storage_size));
      char* new_array_end   = new_array_begin + size() * new_stride;
      char* new_array_cap   = new_array_begin + loop_storage_size;

      for (size_t i = 0; i < size(); ++i) {
        value_type* old_value = reinterpret_cast<value_type*>(
            m_array_begin + i * m_stride);
        value_type* new_value = reinterpret_cast<value_type*>(
            new_array_begin + i * new_stride);

        value_type::move_destroy(new_value, old_value);
      }

      m_aloc.deallocate(m_array_begin);

      m_stride      = new_stride     ;
      m_array_begin = new_array_begin;
      m_array_end   = new_array_end  ;
      m_array_cap   = new_array_cap  ;
    }
  }

  template < typename holder, typename ... holder_ctor_args >
  void create_value(Vtable<CallArgs...>* vtable,
                    holder_ctor_args&&... ctor_args)
  {
    const size_t value_size = sizeof(true_value_type<holder>);

    if (value_size > storage_unused() && value_size <= m_stride) {
      array_reserve(std::max(storage_size() + value_size, 2*storage_capacity()),
                    m_stride);
    } else if (value_size > m_stride) {
      array_reserve((size()+1)*value_size,
                    value_size);
    }

    value_type* value_ptr = reinterpret_cast<value_type*>(m_array_end);
    m_array_end += m_stride;

    value_type::template construct<holder>(
        value_ptr, vtable, std::forward<holder_ctor_args>(ctor_args)...);
  }

  void destroy_value(size_t value_offset)
  {
    value_type* value_ptr =
        reinterpret_cast<value_type*>(m_array_begin + value_offset);
    value_type::destroy(value_ptr);
  }
};


template <typename EXEC_POLICY_T,
          typename ORDER_POLICY_T,
          typename ALLOCATOR_T,
          typename INDEX_T,
          typename ... Args>
struct WorkRunner;


template <typename LoopBody, typename ... Args>
struct HoldBodyArgs_base
{
  template < typename body_in >
  HoldBodyArgs_base(body_in&& body, Args... args)
    : m_body(std::forward<body_in>(body))
    , m_arg_tuple(std::forward<Args>(args)...)
  { }

private:
  LoopBody m_body;
  camp::tuple<Args...> m_arg_tuple;
};

template <typename LoopBody, typename index_type, typename ... Args>
struct HoldBodyArgs_host : HoldBodyArgs_base<LoopBody, Args...>
{
  using base = HoldBodyArgs_base<LoopBody, Args...>;
  using base::base;

  RAJA_INLINE void operator()(index_type i) const
  {
    invoke(i, camp::make_idx_seq_t<sizeof...(Args)>{});
  }

  template < camp::idx_t ... Is >
  RAJA_INLINE void invoke(index_type i, camp::idx_seq<Is...>) const
  {
    this->m_body(i, get<Is>(this->m_arg_tuple)...);
  }
};

template <typename LoopBody, typename index_type, typename ... Args>
struct HoldBodyArgs_device : HoldBodyArgs_base<LoopBody, Args...>
{
  using base = HoldBodyArgs_base<LoopBody, Args...>;
  using base::base;

  RAJA_DEVICE RAJA_INLINE void operator()(index_type i) const
  {
    invoke(i, camp::make_idx_seq_t<sizeof...(Args)>{});
  }

  template < camp::idx_t ... Is >
  RAJA_DEVICE RAJA_INLINE void invoke(index_type i, camp::idx_seq<Is...>) const
  {
    this->m_body(i, get<Is>(this->m_arg_tuple)...);
  }
};

/*!
 * A body and segment holder for storing loops that will be executed as foralls
 */
template <typename ExecutionPolicy, typename Segment_type, typename LoopBody,
          typename index_type, typename ... Args>
struct HoldForall
{
  using HoldBodyArgs = typename std::conditional<
      !type_traits::is_device_exec_policy<ExecutionPolicy>::value,
      HoldBodyArgs_host<LoopBody, index_type, Args...>,
      HoldBodyArgs_device<LoopBody, index_type, Args...> >::type;

  template < typename segment_in, typename body_in >
  HoldForall(segment_in&& segment, body_in&& body)
    : m_segment(std::forward<segment_in>(segment))
    , m_body(std::forward<body_in>(body))
  { }

  RAJA_INLINE void operator()(Args... args) const
  {
    // TODO:: decide when to run hooks, may bypass this and use impl directly
    RAJA::forall<ExecutionPolicy>(
        m_segment,
        HoldBodyArgs{m_body, std::forward<Args>(args)...});
  }

private:
  Segment_type m_segment;
  LoopBody m_body;
};


/*!
 * Runs work in a storage container in order using forall
 */
template <typename FORALL_EXEC_POLICY,
          typename EXEC_POLICY_T,
          typename ORDER_POLICY_T,
          typename ALLOCATOR_T,
          typename INDEX_T,
          typename ... Args>
struct WorkRunnerForallOrdered
{
  using exec_policy = EXEC_POLICY_T;
  using order_policy = ORDER_POLICY_T;
  using Allocator = ALLOCATOR_T;
  using index_type = INDEX_T;

  using forall_exec_policy = FORALL_EXEC_POLICY;

  WorkRunnerForallOrdered() = default;

  WorkRunnerForallOrdered(WorkRunnerForallOrdered const&) = delete;
  WorkRunnerForallOrdered& operator=(WorkRunnerForallOrdered const&) = delete;

  WorkRunnerForallOrdered(WorkRunnerForallOrdered &&) = default;
  WorkRunnerForallOrdered& operator=(WorkRunnerForallOrdered &&) = default;

  // The type  that will hold hte segment and loop body in work storage
  template < typename segment_type, typename loop_type >
  using holder_type = HoldForall<forall_exec_policy, segment_type, loop_type,
                                 index_type, Args...>;

  // The policy indicating where the call function is invoked
  // in this case the values are called on the host in a loop
  using vtable_exec_policy = RAJA::loop_work;

  // runner interfaces with storage to enqueue so the runner can get
  // information from the segment and loop at enqueue time
  template < typename WorkContainer, typename segment_T, typename loop_T >
  inline void enqueue(WorkContainer& storage, segment_T&& seg, loop_T&& loop)
  {
    using holder = holder_type<camp::decay<segment_T>, camp::decay<loop_T>>;
    using vtable_type = typename WorkContainer::vtable_type;

    static vtable_type s_vtable =
        get_Vtable<holder, index_type, Args...>(vtable_exec_policy{});

    storage.template emplace<holder>(&s_vtable,
        std::forward<segment_T>(seg), std::forward<loop_T>(loop));
  }

  // no extra storage required here
  using per_run_storage = int;

  template < typename WorkContainer >
  per_run_storage run(WorkContainer const& storage, Args... args) const
  {
    using value_type = typename WorkContainer::value_type;

    per_run_storage run_storage;

    auto end = storage.end();
    for (auto iter = storage.begin(); iter != end; ++iter) {
      value_type::call(*iter, args...);
    }

    return run_storage;
  }
};

/*!
 * Runs work in a storage container in reverse order using forall
 */
template <typename FORALL_EXEC_POLICY,
          typename EXEC_POLICY_T,
          typename ORDER_POLICY_T,
          typename ALLOCATOR_T,
          typename INDEX_T,
          typename ... Args>
struct WorkRunnerForallReverse
{
  using exec_policy = EXEC_POLICY_T;
  using order_policy = ORDER_POLICY_T;
  using Allocator = ALLOCATOR_T;
  using index_type = INDEX_T;

  using forall_exec_policy = FORALL_EXEC_POLICY;

  WorkRunnerForallReverse() = default;

  WorkRunnerForallReverse(WorkRunnerForallReverse const&) = delete;
  WorkRunnerForallReverse& operator=(WorkRunnerForallReverse const&) = delete;

  WorkRunnerForallReverse(WorkRunnerForallReverse &&) = default;
  WorkRunnerForallReverse& operator=(WorkRunnerForallReverse &&) = default;

  // The type  that will hold hte segment and loop body in work storage
  template < typename segment_type, typename loop_type >
  using holder_type = HoldForall<forall_exec_policy, segment_type, loop_type,
                                 index_type, Args...>;

  // The policy indicating where the call function is invoked
  // in this case the values are called on the host in a loop
  using vtable_exec_policy = RAJA::loop_work;

  // runner interfaces with storage to enqueue so the runner can get
  // information from the segment and loop at enqueue time
  template < typename WorkContainer, typename segment_T, typename loop_T >
  inline void enqueue(WorkContainer& storage, segment_T&& seg, loop_T&& loop)
  {
    using holder = holder_type<camp::decay<segment_T>, camp::decay<loop_T>>;
    using vtable_type = typename WorkContainer::vtable_type;

    static vtable_type s_vtable =
        get_Vtable<holder, index_type, Args...>(vtable_exec_policy{});

    storage.template emplace<holder>(&s_vtable,
        std::forward<segment_T>(seg), std::forward<loop_T>(loop));
  }

  // no extra storage required here
  using per_run_storage = int;

  template < typename WorkContainer >
  per_run_storage run(WorkContainer const& storage, Args... args) const
  {
    using value_type = typename WorkContainer::value_type;

    per_run_storage run_storage;

    auto begin = storage.begin();
    for (auto iter = storage.end(); iter != begin; --iter) {
      value_type::call(*(iter-1), args...);
    }

    return run_storage;
  }
};

}  // namespace detail

}  // namespace RAJA

#endif  // closing endif for header file include guard
