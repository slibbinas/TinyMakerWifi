/* boost::thread pakaitalas WASM'ui: narsykleje viena gija, tad uztenka
   standartiniu std atitikmenu. Naudojama tik uzraktams, ne pacioms gijoms. */
#pragma once
#include <mutex>
#include <condition_variable>
namespace boost {
  template<class M> using lock_guard = std::lock_guard<M>;
  template<class M> using unique_lock = std::unique_lock<M>;
  using mutex = std::mutex;
  using recursive_mutex = std::recursive_mutex;
  using condition_variable = std::condition_variable;
}
