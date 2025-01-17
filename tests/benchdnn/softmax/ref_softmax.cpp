/*******************************************************************************
* Copyright 2019-2022 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "tests/test_thread.hpp"

#include "softmax/softmax.hpp"

namespace softmax {

void compute_ref_fwd(const prb_t *prb, const args_t &args) {
    const dnn_mem_t &src = args.find(DNNL_ARG_SRC);
    const dnn_mem_t &dst = args.find(DNNL_ARG_DST);

    float *dst_ptr = (float *)dst;

    const auto alg = prb->alg;
    int64_t outer_size {0}, inner_size {0}, axis_size {0};
    get_sizes(prb, outer_size, inner_size, axis_size);

    dnnl::impl::parallel_nd(
            outer_size, inner_size, [&](int64_t ou, int64_t in) {
                float space_denom = 0.;
                float space_max = -FLT_MAX;
                int64_t ou_in_offset = ou * axis_size * inner_size + in;

                for (int64_t as = 0; as < axis_size; ++as) {
                    int64_t idx = ou_in_offset + as * inner_size;
                    space_max = MAX2(space_max, src.get_elem(idx));
                }

                for (int64_t as = 0; as < axis_size; ++as) {
                    int64_t idx = ou_in_offset + as * inner_size;
                    float s = src.get_elem(idx);
                    if (alg == SOFTMAX) {
                        float D = dst_ptr[idx] = expf(s - space_max);
                        space_denom += D;
                    } else if (alg == LOGSOFTMAX) {
                        float D = dst_ptr[idx] = s - space_max;
                        space_denom += expf(D);
                    }
                }

                if (alg == SOFTMAX) {
                    space_denom = space_denom ? (1.f / space_denom) : 1.f;
                } else if (alg == LOGSOFTMAX) {
                    space_denom = logf(space_denom);
                }

                for (int64_t as = 0; as < axis_size; ++as) {
                    int64_t idx = ou_in_offset + as * inner_size;
                    if (alg == SOFTMAX) {
                        dst_ptr[idx] *= space_denom;
                    } else if (alg == LOGSOFTMAX) {
                        dst_ptr[idx] -= space_denom;
                    }
                    maybe_oscale(prb->attr, dst_ptr[idx], prb->scales, 0);
                }
            });
}

void compute_ref_bwd(const prb_t *prb, const args_t &args) {
    const dnn_mem_t &dst = args.find(DNNL_ARG_DST);
    const dnn_mem_t &d_dst = args.find(DNNL_ARG_DIFF_DST);
    const dnn_mem_t &d_src = args.find(DNNL_ARG_DIFF_SRC);

    float *d_src_ptr = (float *)d_src;

    const auto alg = prb->alg;
    int64_t outer_size {0}, inner_size {0}, axis_size {0};
    get_sizes(prb, outer_size, inner_size, axis_size);

    dnnl::impl::parallel_nd(
            outer_size, inner_size, [&](int64_t ou, int64_t in) {
                float part_deriv_sum = 0.;
                int64_t ou_in_offset = ou * axis_size * inner_size + in;

                for (int64_t as = 0; as < axis_size; ++as) {
                    int64_t idx = ou_in_offset + as * inner_size;
                    float d = dst.get_elem(idx);
                    float dd = d_dst.get_elem(idx);
                    if (alg == SOFTMAX) {
                        part_deriv_sum += dd * d;
                    } else if (alg == LOGSOFTMAX) {
                        part_deriv_sum += dd;
                    }
                }

                for (int64_t as = 0; as < axis_size; ++as) {
                    int64_t idx = ou_in_offset + as * inner_size;
                    float d = dst.get_elem(idx);
                    float dd = d_dst.get_elem(idx);
                    if (alg == SOFTMAX) {
                        d_src_ptr[idx] = d * (dd - part_deriv_sum);
                    } else if (alg == LOGSOFTMAX) {
                        d_src_ptr[idx] = dd - expf(d) * part_deriv_sum;
                    }
                }
            });
}

void compute_ref(
        const prb_t *prb, const args_t &args, dnnl_primitive_t prim_ref) {
    if (prb->dir & FLAG_FWD)
        compute_ref_fwd(prb, args);
    else
        compute_ref_bwd(prb, args);
}

} // namespace softmax
