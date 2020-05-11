//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-20, Lawrence Livermore National Security, LLC
// and RAJA project contributors. See the RAJA/COPYRIGHT file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

///
/// Source file containing tests for RAJA reducer constructors and initialization.
///

#include "tests/test-reducer-constructors.hpp"

#include "test-reducer-utils.hpp"

using SequentialBasicReducerConstructorTypes = Test< camp::cartesian_product<
                                                        SequentialReducerPolicyList,
                                                        DataTypeList,
                                                        HostResourceList
                                                      >
                             >::Types;

using SequentialInitReducerConstructorTypes = Test< camp::cartesian_product<
                                                        SequentialReducerPolicyList,
                                                        DataTypeList,
                                                        HostResourceList,
                                                        SequentialForoneList
                                                     >
                            >::Types;

INSTANTIATE_TYPED_TEST_SUITE_P(SequentialBasicTest,
                               ReducerBasicConstructorUnitTest,
                               SequentialBasicReducerConstructorTypes);

INSTANTIATE_TYPED_TEST_SUITE_P(SequentialInitTest,
                               ReducerInitConstructorUnitTest,
                               SequentialInitReducerConstructorTypes);


