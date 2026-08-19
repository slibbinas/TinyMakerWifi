/*
 * TBB pakaitalas WASM build'ui.
 *
 * Narsykleje WASM sukasi VIENA gija, tad lygiagretumo bibliotekos nereikia.
 * Cia ne algoritmo keitimas: `parallel_for` virsta tuo paciu ciklu, tik
 * nuosekliu - lygiai tas pats, ka daro paties libslic3r `ExecutionSeq`.
 * Rezultatas nuo to nesikeicia (net tampa deterministiskesnis).
 */
#pragma once
#include <cstddef>
#include <memory>
#include <vector>
#include <utility>
#include <algorithm>

namespace tbb {

template<class I> class blocked_range {
public:
    using const_iterator = I;
    using size_type = std::size_t;
    blocked_range(I b, I e, size_type = 1) : b_(b), e_(e) {}
    I begin() const { return b_; }
    I end()   const { return e_; }
    size_type size() const { return size_type(e_ - b_); }
    bool empty() const { return !(b_ < e_); }
private:
    I b_, e_;
};

template<class I, class J> class blocked_range2d {
public:
    using row_range_type = blocked_range<I>;
    using col_range_type = blocked_range<J>;
    blocked_range2d(I r0, I r1, J c0, J c1) : r_(r0, r1), c_(c0, c1) {}
    const row_range_type &rows() const { return r_; }
    const col_range_type &cols() const { return c_; }
private:
    row_range_type r_; col_range_type c_;
};

struct auto_partitioner {}; struct simple_partitioner {};
struct static_partitioner {}; struct affinity_partitioner {};

template<class I, class F> void parallel_for(const blocked_range<I> &r, const F &f) { f(r); }
template<class I, class F, class P> void parallel_for(const blocked_range<I> &r, const F &f, P &) { f(r); }
template<class I, class F, class P> void parallel_for(const blocked_range<I> &r, const F &f, const P &) { f(r); }
template<class I, class J, class F> void parallel_for(const blocked_range2d<I, J> &r, const F &f) { f(r); }
template<class I, class F> void parallel_for(I first, I last, const F &f) { for (I i = first; i < last; ++i) f(i); }
template<class I, class F> void parallel_for(I first, I last, I step, const F &f) { for (I i = first; i < last; i += step) f(i); }

template<class R, class T, class F, class G>
T parallel_reduce(const R &r, const T &ident, const F &f, const G &) { return f(r, ident); }
template<class R, class T, class F, class G, class P>
T parallel_reduce(const R &r, const T &ident, const F &f, const G &, P &) { return f(r, ident); }

template<class T> using scalable_allocator = std::allocator<T>;
template<class T> using cache_aligned_allocator = std::allocator<T>;

template<class T> class concurrent_vector : public std::vector<T> {
public:
    using base = std::vector<T>;
    using base::base;
    using iterator = typename base::iterator;
    iterator push_back(const T &v) { base::push_back(v); return base::end() - 1; }
    iterator push_back(T &&v) { base::push_back(std::move(v)); return base::end() - 1; }
    template<class... A> iterator emplace_back(A &&...a) {
        base::emplace_back(std::forward<A>(a)...); return base::end() - 1; }
    iterator grow_by(std::size_t n) { std::size_t o = base::size(); base::resize(o + n); return base::begin() + o; }
};

struct spin_mutex {
    struct scoped_lock { scoped_lock() {} scoped_lock(spin_mutex &) {}
        void acquire(spin_mutex &) {} void release() {} };
    void lock() {} void unlock() {} bool try_lock() { return true; }
};
using mutex = spin_mutex;
using queuing_mutex = spin_mutex;

class task_group {
public:
    template<class F> void run(const F &f) { f(); }
    void wait() {}
};

class task_arena {
public:
    task_arena() {} explicit task_arena(int) {}
    static constexpr int automatic = -1;
    template<class F> void execute(const F &f) { f(); }
    void initialize() {} void initialize(int) {}
    static int max_concurrency() { return 1; }
};

class global_control {
public:
    enum parameter { max_allowed_parallelism, thread_stack_size };
    global_control(parameter, std::size_t) {}
    static std::size_t active_value(parameter) { return 1; }
};

template<class T> class enumerable_thread_specific {
public:
    enumerable_thread_specific() = default;
    template<class F> explicit enumerable_thread_specific(F f) : v_(1, f()) {}
    explicit enumerable_thread_specific(const T &t) : v_(1, t) {}
    T &local() { if (v_.empty()) v_.emplace_back(); return v_.front(); }
    T &local(bool &exists) { exists = !v_.empty(); return local(); }
    auto begin() { return v_.begin(); } auto end() { return v_.end(); }
    auto begin() const { return v_.begin(); } auto end() const { return v_.end(); }
    std::size_t size() const { return v_.size(); }
    void clear() { v_.clear(); }
private:
    std::vector<T> v_;
};

class task_scheduler_observer {
public:
    task_scheduler_observer() {} explicit task_scheduler_observer(task_arena &) {}
    virtual ~task_scheduler_observer() {}
    virtual void on_scheduler_entry(bool) {}
    virtual void on_scheduler_exit(bool) {}
    void observe(bool = true) {}
};

namespace this_task_arena {
    inline int max_concurrency() { return 1; }
    inline int current_thread_index() { return 0; }
    template<class F> auto isolate(const F &f) -> decltype(f()) { return f(); }
}

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }

/* Versijos makro: libslic3r tikrina static_assert'u, ar TBB antrastes matomos. */
#ifndef TBB_VERSION_MAJOR
#define TBB_VERSION_MAJOR 2021
#define TBB_VERSION_MINOR 11
#define TBB_INTERFACE_VERSION 12110
#endif
