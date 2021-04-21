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

  unsigned seed = get_random_seed();

  RAJA::TypedRangeSegment<int> seg(0, node_size);

  RandomGraph<graph_type> g(seed);

  const int num_nodes = g.num_nodes();

  std::vector<int> node_data_check(num_nodes, 1);

  // node data pointers and resources
  // TODO: fix data allocation so it is visible everywhere
  std::vector<int*>                      node_previous(num_nodes, nullptr);
  std::vector<int*>                      node_data(num_nodes, nullptr);
  std::vector<camp::resources::Resource> node_res(num_nodes, camp::resources::Resource(camp::resources::Host()));

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
        g.add_node(node_id, std::move(edges_to_node),
            RAJA::expt::graph::Empty());
      }
      else if (++node_type_id == final_type_id) {
        add_node_data(camp::resources::Host());
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, std::move(edges_to_node),
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
        add_node_data(camp::resources::Host());
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, std::move(edges_to_node),
            RAJA::expt::graph::Forall<RAJA::loop_exec>(seg, [=](int i){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = node_data[other_id];
            my_data[i] += other_data[i];
          }
        }));
      }
#if defined(RAJA_ENABLE_OPENMP)
      else if (++node_type_id == final_type_id) {
        add_node_data(camp::resources::Host());
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, std::move(edges_to_node),
            RAJA::expt::graph::Forall<RAJA::omp_parallel_exec<RAJA::omp_for_nowait_exec>>(seg, [=](int i){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = node_data[other_id];
            my_data[i] += other_data[i];
          }
        }));
      }
#endif
#if defined(RAJA_ENABLE_TBB)
      else if (++node_type_id == final_type_id) {
        add_node_data(camp::resources::Host());
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, std::move(edges_to_node),
            RAJA::expt::graph::Forall<RAJA::tbb_for_exec>(seg, [=](int i){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = node_data[other_id];
            my_data[i] += other_data[i];
          }
        }));
      }
#endif
#if defined(RAJA_ENABLE_TARGET_OPENMP)
      else if (++node_type_id == final_type_id) {
        add_node_data(camp::resources::Omp());
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, std::move(edges_to_node),
            RAJA::expt::graph::Forall<RAJA::omp_target_parallel_for_exec_nt>(seg, [=](int i){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = node_data[other_id];
            my_data[i] += other_data[i];
          }
        }));
      }
#endif
#if defined(RAJA_ENABLE_CUDA)
      else if (++node_type_id == final_type_id) {
        add_node_data(camp::resources::Cuda());
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, std::move(edges_to_node),
            RAJA::expt::graph::Forall<RAJA::cuda_exec_async<128>>(seg, [=]RAJA_DEVICE(int i){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = node_data[other_id];
            my_data[i] += other_data[i];
          }
        }));
      }
#endif
#if defined(RAJA_ENABLE_HIP)
      else if (++node_type_id == final_type_id) {
        add_node_data(camp::resources::Hip());
        int* previous = node_previous[node_id];
        int* my_data  = node_data[node_id];
        g.add_node(node_id, std::move(edges_to_node),
            RAJA::expt::graph::Forall<RAJA::hip_exec_async<128>>(seg, [=]RAJA_DEVICE(int i){
          for (int e = 0; e < num_edges_to_node; ++e) {
            int other_id = previous[e];
            int* other_data = node_data[other_id];
            my_data[i] += other_data[i];
          }
        }));
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
