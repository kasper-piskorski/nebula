/* Copyright (c) 2018 - present, VE Software Inc. All rights reserved
 *
 * This source code is licensed under Apache 2.0 License
 *  (found in the LICENSE.Apache file in the root directory)
 */

#include <folly/ExceptionWrapper.h>

namespace vesoft {

// Not thread-safe, all futures need to be on the same executor
template <class FutureIter, typename ResultEval>
folly::Future<SucceededResultList<FutureIter>> collectNSucceeded(
        FutureIter first,
        FutureIter last,
        size_t n,
        ResultEval eval) {
    using Result = SucceededResultList<FutureIter>;

    struct Context {
        Result results;
        std::atomic<size_t> numCompleted = {0};
        folly::Promise<Result> promise;
        size_t nTotal;
    };

    size_t total = size_t(std::distance(first, last));
    DCHECK_GT(n, 0U);
    DCHECK_GE(total, 0U);

    if (total < n) {
        return folly::Future<Result>(
            folly::exception_wrapper(
                std::runtime_error("Not enough futures")));
    }

    auto ctx = std::make_shared<Context>();
    ctx->nTotal = total;

    // for each succeeded Future, add to the result list, until
    // we have rquired number of futures, at which point we fulfil
    // the promise with the result list
    for (; first != last; ++first) {
        first->setCallback_([n, ctx, eval] (
                folly::Try<FutureReturnType<FutureIter>>&& t) {
            if (!ctx->promise.isFulfilled()) {
                if (!t.hasException() && eval(t.value())) {
                    ctx->results.emplace_back(std::move(t.value()));
                }
                if ((++ctx->numCompleted) == ctx->nTotal ||
                    ctx->results.size() == n) {
                    // Done
                    VLOG(2) << "Set Value [completed="
                            << ctx->numCompleted
                            << ", total=" << ctx->nTotal
                            << ", Result list size="
                            << ctx->results.size()
                            << "]";
                    ctx->promise.setValue(std::move(ctx->results));
                }
            }
        });
    }

    return ctx->promise.getFuture();
}

}  // namespace vesoft