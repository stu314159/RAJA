//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-21, Lawrence Livermore National Security, LLC
// and RAJA project contributors. See the RAJA/COPYRIGHT file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

#ifndef __TEST_GRAPH_MIXEDNODES_HPP__
#define __TEST_GRAPH_MIXEDNODES_HPP__

#include <numeric>

template <typename GRAPH_POLICY, typename WORKING_RES>
void MixedNodesTestImpl(int node_size)
{
  using graph_type = RAJA::expt::graph::DAG<GRAPH_POLICY, WORKING_RES>;

  auto r = WORKING_RES::get_default();

  camp::resources::Host host_res = camp::resources::Host::get_default();
#if defined(RAJA_ENABLE_TARGET_OPENMP)
  camp::resources::Omp  omp_res = camp::resources::Omp::get_default();
#endif
#if defined(RAJA_ENABLE_CUDA)
  camp::resources::Cuda cuda_res = camp::resources::Cuda::get_default();
#endif
#if defined(RAJA_ENABLE_HIP)
  camp::resources::Hip  hip_res = camp::resources::Hip::get_default();
#endif

  unsigned seed = get_random_seed();

  RAJA::TypedRangeSegment<int> seg(0, node_size);

  RandomGraph<graph_type> g(seed);

  const int num_nodes = g.num_nodes();

  std::vector<int> node_data_check(num_nodes, 1);

  // node data pointers and resources
  // TODO: fix data allocation so it is visible everywhere
  int** node_previous = host_res.allocate<int*>(num_nodes);
  host_res.memset(node_previous, 0, num_nodes*sizeof(int*));
  std::vector<camp::resources::Resource> node_res(num_nodes, camp::resources::Resource(host_res));

  int** node_data     = host_res.allocate<int*>(num_nodes);
  host_res.memset(node_data,     0, num_nodes*sizeof(int*));
  host_res.wait();
#if defined(RAJA_ENABLE_TARGET_OPENMP)
  int** omp_node_data = omp_res.allocate<int*>(num_nodes);
  omp_res.memset(omp_node_data, 0, num_nodes*sizeof(int*));
  omp_res.wait();
#endif
#if defined(RAJA_ENABLE_CUDA)
  int** cuda_node_data = cuda_res.allocate<int*>(num_nodes);
  cuda_res.memset(cuda_node_data, 0, num_nodes*sizeof(int*));
  cuda_res.wait();
#endif
#if defined(RAJA_ENABLE_HIP)
  int** hip_node_data = hip_res.allocate<int*>(num_nodes);
  hip_res.memset(hip_node_data, 0, num_nodes*sizeof(int*));
  hip_res.wait();
#endif

  auto add_node = [&](int node_id){

    std::vector<int> edges_to_node = g.get_dependencies(node_id);

    int num_edges_to_node = edges_to_node.size();

    auto add_node_data = [&](camp::resources::Resource res) {
      node_previous[node_id] = res.allocate<int>(num_edges_to_node);
      res.memcpy(node_previous[node_id], &edges_to_node[0], sizeof(int) * num_edges_to_node);
      node_data[node_id] = res.allocate<int>(node_size);
      std::vector<int> ones(node_size, 1);
      res.memcpy(node_data[node_id], &ones[0], sizeof(int) * node_size);
      node_res[node_id] = res;
      res.get_event().wait();
    };

    int final_type_id = -1;

    // do this twice, once to count the number of possible node types
    // again to pick a random node type
    int max_type_id = -1;
    while (final_type_id == -1) {

      if (max_type_id != -1) {
        final_type_id = std::uniform_int_distribution<int>(0, max_type_id)(g.rng());
      }

      int node_type_id = -1;
      if (++node_type_id == final_type_id) {
        add_node_data(camp::resources::Host());
        g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::Empty());
      }
      else if (++node_type_id == final_type_id) {
        add_node_data(camp::resources::Host());
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::Function([=](){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = node_data[other_id];
            for (int i = 0; i < node_size; ++i) {
              my_data[i] += other_data[i];
            }
          }
        }));
      }
      else if (++node_type_id == final_type_id) {
        add_node_data(host_res);
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::Forall<RAJA::loop_exec>(seg, [=](int i){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = node_data[other_id];
            my_data[i] += other_data[i];
          }
        }));
      }
      else if (++node_type_id == final_type_id) {
        add_node_data(host_res);
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        using Allocator = typename detail::ResourceAllocator<camp::resources::Host>::template std_allocator<char>;
        auto& n = g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::WorkGroup<
             RAJA::WorkGroupPolicy<RAJA::loop_work, RAJA::ordered, RAJA::constant_stride_array_of_objects>,
             int,
             RAJA::xargs<>,
             Allocator
           >(Allocator(host_res)));
        for (int e = 0; e < num_edges_to_node; ++e) {
          int other_id = previous[e];
          int* other_data = node_data[other_id];
          n.enqueue(seg, [=](int i){
            my_data[i] += other_data[i];
          });
        }
      }
#if defined(RAJA_ENABLE_OPENMP)
      else if (++node_type_id == final_type_id) {
        add_node_data(host_res);
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::Forall<RAJA::omp_parallel_exec<RAJA::omp_for_exec>>(seg, [=](int i){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = node_data[other_id];
            my_data[i] += other_data[i];
          }
        }));
      }
      else if (++node_type_id == final_type_id) {
        add_node_data(host_res);
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        using Allocator = typename detail::ResourceAllocator<camp::resources::Host>::template std_allocator<char>;
        auto& n = g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::WorkGroup<
             RAJA::WorkGroupPolicy<RAJA::omp_work, RAJA::ordered, RAJA::constant_stride_array_of_objects>,
             int,
             RAJA::xargs<>,
             Allocator
           >(Allocator(host_res)));
        for (int e = 0; e < num_edges_to_node; ++e) {
          int other_id = previous[e];
          int* other_data = node_data[other_id];
          n.enqueue(seg, [=](int i){
            my_data[i] += other_data[i];
          });
        }
      }
#endif
#if defined(RAJA_ENABLE_TBB)
      else if (++node_type_id == final_type_id) {
        add_node_data(host_res);
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::Forall<RAJA::tbb_for_exec>(seg, [=](int i){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = node_data[other_id];
            my_data[i] += other_data[i];
          }
        }));
      }
      else if (++node_type_id == final_type_id) {
        add_node_data(host_res);
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        using Allocator = typename detail::ResourceAllocator<camp::resources::Host>::template std_allocator<char>;
        auto& n = g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::WorkGroup<
             RAJA::WorkGroupPolicy<RAJA::tbb_work, RAJA::ordered, RAJA::constant_stride_array_of_objects>,
             int,
             RAJA::xargs<>,
             Allocator
           >(Allocator(host_res)));
        for (int e = 0; e < num_edges_to_node; ++e) {
          int other_id = previous[e];
          int* other_data = node_data[other_id];
          n.enqueue(seg, [=](int i){
            my_data[i] += other_data[i];
          });
        }
      }
#endif
#if defined(RAJA_ENABLE_TARGET_OPENMP)
      else if (++node_type_id == final_type_id) {
        add_node_data(omp_res);
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::Forall<RAJA::omp_target_parallel_for_exec_nt>(seg, [=](int i){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = omp_node_data[other_id];
            my_data[i] += other_data[i];
          }
        }));
      }
      else if (++node_type_id == final_type_id) {
        add_node_data(omp_res);
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        using Allocator = typename detail::ResourceAllocator<camp::resources::Omp>::template std_allocator<char>;
        auto& n = g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::WorkGroup<
             RAJA::WorkGroupPolicy<RAJA::omp_target_work, RAJA::ordered, RAJA::constant_stride_array_of_objects>,
             int,
             RAJA::xargs<>,
             Allocator
           >(Allocator(omp_res)));
        for (int e = 0; e < num_edges_to_node; ++e) {
          int other_id = previous[e];
          int* other_data = node_data[other_id];
          n.enqueue(seg, [=](int i){
            my_data[i] += other_data[i];
          });
        }
      }
#endif
#if defined(RAJA_ENABLE_CUDA)
      else if (++node_type_id == final_type_id) {
        add_node_data(cuda_res);
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::Forall<RAJA::cuda_exec_async<128>>(seg, [=]RAJA_DEVICE(int i){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = cuda_node_data[other_id];
            my_data[i] += other_data[i];
          }
        }));
      }
      else if (++node_type_id == final_type_id) {
        add_node_data(cuda_res);
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        using Allocator = typename detail::ResourceAllocator<camp::resources::Cuda>::template std_allocator<char>;
        auto& n = g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::WorkGroup<
             RAJA::WorkGroupPolicy<RAJA::cuda_work_async<1024>,
                                   RAJA::unordered_cuda_loop_y_block_iter_x_threadblock_average,
                                   RAJA::constant_stride_array_of_objects>,
             int,
             RAJA::xargs<>,
             Allocator
           >(Allocator(cuda_res)));
        for (int e = 0; e < num_edges_to_node; ++e) {
          int other_id = previous[e];
          int* other_data = node_data[other_id];
          n.enqueue(seg, [=]RAJA_DEVICE(int i){
            my_data[i] += other_data[i];
          });
        }
      }
#endif
#if defined(RAJA_ENABLE_HIP)
      else if (++node_type_id == final_type_id) {
        add_node_data(hip_res);
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::Forall<RAJA::hip_exec_async<128>>(seg, [=]RAJA_DEVICE(int i){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = hip_node_data[other_id];
            my_data[i] += other_data[i];
          }
        }));
      }
      else if (++node_type_id == final_type_id) {
        add_node_data(hip_res);
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        using Allocator = typename detail::ResourceAllocator<camp::resources::Hip>::template std_allocator<char>;
        auto& n = g.add_node(node_id, edges_to_node,
            RAJA::expt::graph::WorkGroup<
             RAJA::WorkGroupPolicy<RAJA::hip_work_async<1024>,
#if defined(RAJA_ENABLE_HIP_INDIRECT_FUNCTION_CALL)
                                   RAJA::unordered_hip_loop_y_block_iter_x_threadblock_average,
#else
                                   RAJA::ordered,
#endif
                                   RAJA::constant_stride_array_of_objects>,
             int,
             RAJA::xargs<>,
             Allocator
           >(Allocator(hip_res)));
        for (int e = 0; e < num_edges_to_node; ++e) {
          int other_id = previous[e];
          int* other_data = node_data[other_id];
          n.enqueue(seg, [=]RAJA_DEVICE(int i){
            my_data[i] += other_data[i];
          });
        }
      }
#endif

      if (max_type_id == -1) {
        max_type_id = node_type_id;
      } else {
        assert(0 <= final_type_id);
        assert(final_type_id <= max_type_id);
      }
    }

    // not empty node, count up contributions
    if (final_type_id != 0) {
      for (int e = 0; e < num_edges_to_node; ++e) {
        int other_id = edges_to_node[e];
        node_data_check[node_id] += node_data_check[other_id];
      }
    }
  };

  // add nodes
  for (int node_id = 0; node_id < num_nodes; ++node_id) {

    add_node(node_id);
  }

  // copy pointers to platform specific memory
#if defined(RAJA_ENABLE_TARGET_OPENMP)
  omp_res.memcpy(omp_node_data, node_data, num_nodes*sizeof(int*));
  omp_res.wait();
#endif
#if defined(RAJA_ENABLE_CUDA)
  cuda_res.memcpy(cuda_node_data, node_data, num_nodes*sizeof(int*));
  cuda_res.wait();
#endif
#if defined(RAJA_ENABLE_HIP)
  hip_res.memcpy(hip_node_data, node_data, num_nodes*sizeof(int*));
  hip_res.wait();
#endif

  g.graph().exec(r);
  r.wait();


  // check data
  for (int node_id = 0; node_id < num_nodes; ++node_id) {
    for (int i = 0; i < node_size; ++i) {
      ASSERT_EQ(node_data_check[node_id], node_data[node_id][i]);
    }
  }

  // deallocate node data
  for (int node_id = 0; node_id < num_nodes; ++node_id) {
    node_res[node_id].deallocate(node_previous[node_id]);
    node_res[node_id].deallocate(node_data[node_id]);
  }

  host_res.deallocate(node_previous);
  host_res.deallocate(node_data);
#if defined(RAJA_ENABLE_TARGET_OPENMP)
  omp_res.deallocate(omp_node_data);
#endif
#if defined(RAJA_ENABLE_CUDA)
  cuda_res.deallocate(cuda_node_data);
#endif
#if defined(RAJA_ENABLE_HIP)
  hip_res.deallocate(hip_node_data);
#endif

}


TYPED_TEST_SUITE_P(MixedNodesTest);
template <typename T>
class MixedNodesTest : public ::testing::Test
{
};


TYPED_TEST_P(MixedNodesTest, MixedNodes)
{
  using GRAPH_POLICY = typename camp::at<TypeParam, camp::num<0>>::type;
  using WORKING_RES  = typename camp::at<TypeParam, camp::num<1>>::type;

  MixedNodesTestImpl<GRAPH_POLICY, WORKING_RES>(1);
  MixedNodesTestImpl<GRAPH_POLICY, WORKING_RES>(27);
  MixedNodesTestImpl<GRAPH_POLICY, WORKING_RES>(1039);
}

REGISTER_TYPED_TEST_SUITE_P(MixedNodesTest,
                            MixedNodes);

#endif  // __TEST_GRAPH_MIXEDNODES_HPP__