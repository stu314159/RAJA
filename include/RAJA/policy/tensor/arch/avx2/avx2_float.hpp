/*!
 ******************************************************************************
 *
 * \file
 *
 * \brief   RAJA header file defining a SIMD register abstraction.
 *
 ******************************************************************************
 */

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-19, Lawrence Livermore National Security, LLC
// and RAJA project contributors. See the RAJA/COPYRIGHT file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

#ifdef __AVX2__

#ifndef RAJA_policy_vector_register_avx2_float_HPP
#define RAJA_policy_vector_register_avx2_float_HPP

#include "RAJA/config.hpp"
#include "RAJA/util/macros.hpp"
#include "RAJA/pattern/tensor/TensorRegister.hpp"

// Include SIMD intrinsics header file
#include <immintrin.h>
#include <cmath>


namespace RAJA
{

  template<camp::idx_t SKEW>
  class TensorRegister<avx2_register, float, VectorLayout, camp::idx_seq<8>, camp::idx_seq<0,1,2,3,4,5,6,7>, SKEW> :
    public internal::TensorRegisterBase<TensorRegister<avx2_register, float, VectorLayout, camp::idx_seq<8>, camp::idx_seq<0,1,2,3,4,5,6,7>, SKEW>>
  {
    public:
      using register_policy = avx2_register;
      using self_type = TensorRegister<avx2_register, float, VectorLayout, camp::idx_seq<8>, camp::idx_seq<0,1,2,3,4,5,6,7>, SKEW>;
      using element_type = float;
      using register_type = __m256;


    private:
      register_type m_value;

      RAJA_INLINE
      __m256i createMask(camp::idx_t N) const {
        // Generate a mask
        return  _mm256_set_epi32(
            N >= 8 ? -1 : 0,
            N >= 7 ? -1 : 0,
            N >= 6 ? -1 : 0,
            N >= 5 ? -1 : 0,
            N >= 4 ? -1 : 0,
            N >= 3 ? -1 : 0,
            N >= 2 ? -1 : 0,
            -1);
      }

      RAJA_INLINE
      __m256i createStridedOffsets(camp::idx_t stride) const {
        // Generate a strided offset list
        return  _mm256_set_epi32(
            7*stride, 6*stride, 5*stride, 4*stride,
            3*stride, 2*stride, stride, 0);
      }

      RAJA_INLINE
      __m256i createPermute1(camp::idx_t N) const {
        // Generate a permutation for first round of min/max routines
        return  _mm256_set_epi32(
            N >= 7 ? 6 : 0,
            N >= 8 ? 7 : 0,
            N >= 5 ? 4 : 0,
            N >= 6 ? 5 : 0,
            N >= 3 ? 2 : 0,
            N >= 4 ? 3 : 0,
            N >= 1 ? 0 : 0,
            N >= 2 ? 1 : 0);
      }

      RAJA_INLINE
      __m256i createPermute2(camp::idx_t N) const {
        // Generate a permutation for second round of min/max routines
        return  _mm256_set_epi32(
            N >= 6 ? 5 : 0,
            N >= 5 ? 4 : 0,
            N >= 8 ? 7 : 0,
            N >= 7 ? 6 : 0,
            N >= 2 ? 1 : 0,
            N >= 1 ? 0 : 0,
            N >= 4 ? 3 : 0,
            N >= 2 ? 2 : 0);
      }

    public:

      static constexpr camp::idx_t s_num_elem = 8;

      /*!
       * @brief Default constructor, zeros register contents
       */
      RAJA_INLINE
      TensorRegister() : m_value(_mm256_setzero_ps()) {
      }

      /*!
       * @brief Copy constructor from underlying simd register
       */
      RAJA_INLINE
      constexpr
      explicit TensorRegister(register_type const &c) : m_value(c) {}


      /*!
       * @brief Copy constructor
       */
      RAJA_INLINE
      constexpr
      TensorRegister(self_type const &c) : m_value(c.m_value) {}

      /*!
       * @brief Copy assignment constructor
       */
      RAJA_INLINE
      self_type &operator=(self_type const &c){
        m_value = c.m_value;
        return *this;
      }

      /*!
       * @brief Construct from scalar.
       * Sets all elements to same value (broadcast).
       */
      RAJA_INLINE
      TensorRegister(element_type const &c) : m_value(_mm256_set1_ps(c)) {}


      /*!
       * @brief Load a full register from a stride-one memory location
       *
       */
      RAJA_INLINE
      self_type &load_packed(element_type const *ptr){
        m_value = _mm256_loadu_ps(ptr);
        return *this;
      }

      /*!
       * @brief Partially load a register from a stride-one memory location given
       *        a run-time number of elements.
       *
       */
      RAJA_INLINE
      self_type &load_packed_n(element_type const *ptr, camp::idx_t N){
        m_value = _mm256_maskload_ps(ptr, createMask(N));
        return *this;
      }

      /*!
       * @brief Gather a full register from a strided memory location
       *
       */
      RAJA_INLINE
      self_type &load_strided(element_type const *ptr, camp::idx_t stride){
        m_value = _mm256_i32gather_ps(ptr,
                                      createStridedOffsets(stride),
                                      sizeof(element_type));
        return *this;
      }


      /*!
       * @brief Partially load a register from a stride-one memory location given
       *        a run-time number of elements.
       *
       */
      RAJA_INLINE
      self_type &load_strided_n(element_type const *ptr, camp::idx_t stride, camp::idx_t N){
        m_value = _mm256_mask_i32gather_ps(_mm256_setzero_ps(),
                                      ptr,
                                      createStridedOffsets(stride),
                                      _mm256_castsi256_ps(createMask(N)),
                                      sizeof(element_type));
        return *this;
      }


      /*!
       * @brief Store entire register to consecutive memory locations
       *
       */
      RAJA_INLINE
      self_type const &store_packed(element_type *ptr) const{
        _mm256_storeu_ps(ptr, m_value);
        return *this;
      }

      /*!
       * @brief Store entire register to consecutive memory locations
       *
       */
      RAJA_INLINE
      self_type const &store_packed_n(element_type *ptr, camp::idx_t N) const{
        _mm256_maskstore_ps(ptr, createMask(N), m_value);
        return *this;
      }

      /*!
       * @brief Store entire register to consecutive memory locations
       *
       */
      RAJA_INLINE
      self_type const &store_strided(element_type *ptr, camp::idx_t stride) const{
        for(camp::idx_t i = 0;i < 8;++ i){
          ptr[i*stride] = m_value[i];
        }
        return *this;
      }


      /*!
       * @brief Store partial register to consecutive memory locations
       *
       */
      RAJA_INLINE
      self_type const &store_strided_n(element_type *ptr, camp::idx_t stride, camp::idx_t N) const{
        for(camp::idx_t i = 0;i < N;++ i){
          ptr[i*stride] = m_value[i];
        }
        return *this;
      }



      /*!
       * @brief Get scalar value from vector register
       * @param i Offset of scalar to get
       * @return Returns scalar value at i
       */
      RAJA_INLINE
      element_type get(camp::idx_t i) const
      {return m_value[i];}


      /*!
       * @brief Set scalar value in vector register
       * @param i Offset of scalar to set
       * @param value Value of scalar to set
       */
      RAJA_INLINE
      self_type &set(camp::idx_t i, element_type value)
      {
        m_value[i] = value;
        return *this;
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &broadcast(element_type const &value){
        m_value =  _mm256_set1_ps(value);
        return *this;
      }


      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &copy(self_type const &src){
        m_value = src.m_value;
        return *this;
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type add(self_type const &b) const {
        return self_type(_mm256_add_ps(m_value, b.m_value));
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type subtract(self_type const &b) const {
        return self_type(_mm256_sub_ps(m_value, b.m_value));
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type multiply(self_type const &b) const {
        return self_type(_mm256_mul_ps(m_value, b.m_value));
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type divide(self_type const &b, camp::idx_t = 8) const {
        return self_type(_mm256_div_ps(m_value, b.m_value));
      }

// only use FMA's if the compiler has them turned on
#ifdef __FMA__
      RAJA_INLINE
      RAJA_HOST_DEVICE
      self_type fused_multiply_add(self_type const &b, self_type const &c) const
      {
        return self_type(_mm256_fmadd_ps(m_value, b.m_value, c.m_value));
      }

      RAJA_INLINE
      RAJA_HOST_DEVICE
      self_type fused_multiply_subtract(self_type const &b, self_type const &c) const
      {
        return self_type(_mm256_fmsub_ps(m_value, b.m_value, c.m_value));
      }
#endif

      /*!
       * @brief Sum the elements of this vector
       * @return Sum of the values of the vectors scalar elements
       */
      RAJA_INLINE
      element_type sum(camp::idx_t N = 8) const
      {
        // Some simple cases
        if(N == 1){
          return m_value[0];
        }
        if(N == 2){
          return m_value[0]+m_value[1];
        }

        // swap odd-even pairs and add
        auto sh1 = _mm256_permute_ps(m_value, 0xB1);
        auto red1 = _mm256_add_ps(m_value, sh1);

        if(N == 3 || N == 4){
          return red1[0] + red1[2];
        }

        // swap odd-even quads and add
        auto sh2 = _mm256_permute_ps(red1, 0x4E);
        auto red2 = _mm256_add_ps(red1, sh2);

        return red2[0] + red2[4];
      }


      /*!
       * @brief Returns the largest element
       * @return The largest scalar element in the register
       */
      RAJA_INLINE
      element_type max(camp::idx_t N = 8) const
      {
        // Some simple cases
        if(N <= 0 || N >8){
          return RAJA::operators::limits<float>::min();
        }
        if(N == 1){
          return m_value[0];
        }
        if(N == 2){
          return std::max<element_type>(m_value[0], m_value[1]);
        }

        // swap odd-even pairs and add
        auto sh1 = _mm256_permutevar8x32_ps(m_value, createPermute1(N));
        auto red1 = _mm256_max_ps(m_value, sh1);

        if(N == 3){
          return std::max<element_type>(red1[0], m_value[2]);
        }
        if(N == 4){
          return std::max<element_type>(red1[0], red1[2]);
        }

        // swap odd-even quads and add
        auto sh2 = _mm256_permutevar8x32_ps(red1, createPermute2(N));
        auto red2 = _mm256_max_ps(red1, sh2);

        return std::max<element_type>(red2[0], red2[4]);

      }

      /*!
       * @brief Returns element-wise largest values
       * @return Vector of the element-wise max values
       */
      RAJA_INLINE
      self_type vmax(self_type a) const
      {
        return self_type(_mm256_max_ps(m_value, a.m_value));
      }

      /*!
       * @brief Returns the largest element
       * @return The largest scalar element in the register
       */
      RAJA_INLINE
      element_type min(camp::idx_t N = 8) const
      {
        // Some simple cases
        if(N <= 0 || N >8){
          return RAJA::operators::limits<float>::max();
        }
        if(N == 1){
          return m_value[0];
        }
        if(N == 2){
          return std::min<element_type>(m_value[0], m_value[1]);
        }

        // swap odd-even pairs and add
        auto sh1 = _mm256_permutevar8x32_ps(m_value, createPermute1(N));
        auto red1 = _mm256_min_ps(m_value, sh1);

        if(N == 3){
          return std::min<element_type>(red1[0], m_value[2]);
        }
        if(N == 4){
          return std::min<element_type>(red1[0], red1[2]);
        }

        // swap odd-even quads and add
        auto sh2 = _mm256_permutevar8x32_ps(red1, createPermute2(N));
        auto red2 = _mm256_min_ps(red1, sh2);

        return std::min<element_type>(red2[0], red2[4]);
      }

      /*!
       * @brief Returns element-wise largest values
       * @return Vector of the element-wise max values
       */
      RAJA_INLINE
      self_type vmin(self_type a) const
      {
        return self_type(_mm256_min_ps(m_value, a.m_value));
      }
  };



}  // namespace RAJA


#endif

#endif //__AVX2__