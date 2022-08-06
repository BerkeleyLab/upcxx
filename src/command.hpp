#ifndef _3589ecdd_36fa_4240_ade2_c807b184ac7f
#define _3589ecdd_36fa_4240_ade2_c807b184ac7f

#include <upcxx/diagnostic.hpp>
#include <upcxx/future.hpp>
#include <upcxx/global_fnptr.hpp>
#include <upcxx/serialization.hpp>

// Commands are callable objects that have been packed into a parcel.

namespace upcxx {
namespace detail {
  // command<Arg...>: Collection of static functions for managing commands which
  // accept argument list of type Arg... when executed.
  template<typename ...Arg>
  class command {
    using executor_wire_t = global_fnptr<void(Arg...)>;
    
    template<typename Fn, bool fn_on_heap, void(*cleanup)(Arg...)>
    struct after_execute {
      Fn *fn; // to be cleaned up
      std::tuple<Arg...> a;
      template<typename ...T>
      void operator()(T&&...) {
        fn->~Fn();
        if (fn_on_heap) {
          ::operator delete(fn);
        }
        detail::apply_tupled(cleanup, std::move(a));
      }
    };

#ifndef UPCXXI_RPC_STACK_MAX_FN_SIZE
#define UPCXXI_RPC_STACK_MAX_FN_SIZE 2048
#endif

    template<typename Fn, detail::serialization_reader(*reader)(Arg...), void(*cleanup)(Arg...)>
    static void the_executor(Arg ...a) {
      detail::serialization_reader r = reader(a...);
      
      r.template read<executor_wire_t>();

      using FnDez = typename serialization_traits<Fn>::deserialized_type;

      // FnDez is stored on the stack if it is less than the size
      // limit and does not return a non-ready future.
      // - If FnDez is too big, store it on the heap to avoid smashing
      //   the stack.
      // - If FnDez returns a non-ready future, we need to extend its
      //   lifetime (along with its bound arguments) until the future
      //   is ready. So we store it on the heap and destroy it after
      //   the future is ready.
      constexpr bool is_trivial =
        future_is_trivially_ready<
          typename detail::apply_variadic_as_future<FnDez&&>::return_type
        >::value;
      constexpr bool is_small =
        sizeof(FnDez) <= UPCXXI_RPC_STACK_MAX_FN_SIZE;
      constexpr bool on_stack = is_trivial && is_small;
      using local_storage_t =
        typename std::conditional<on_stack,
                                  detail::raw_storage<FnDez>,
                                  int>::type;

      local_storage_t storage;
      void *spot = on_stack ? (void*)&storage : ::operator new(sizeof(FnDez));
      FnDez *fn;
      using wrapper_t = detail::serialization_storage_wrapper<FnDez*>;
      UPCXXI_ASSERT_NOEXCEPTIONS_BEGIN
        fn = serialization_traits<Fn>::deserialize(r, wrapper_t{spot});
      UPCXXI_ASSERT_NOEXCEPTIONS_END

      // after_execute<...>() will cleanup fn. This will happen
      // immediately after fn is invoked if fn returns a non-future or
      // ready future, otherwise it is deferred until after the future
      // returned by fn is ready.
      detail::apply_as_future_then_lazy(
        std::move(*fn),
        after_execute<FnDez, !on_stack, cleanup>{
          fn, std::tuple<Arg...>(a...)
        }
      );
    }

  public:
    using executor_t = void(*)(Arg...);
    
    // Given a reader in the same state as the one passed into `command::serialize`,
    // this will retrieve the executor function.
    static executor_t get_executor(detail::serialization_reader r) {
      executor_wire_t exec = r.template read<executor_wire_t>();
      return exec.detokenize();
    }

    // Update an upper-bound on the size needed to accomadate adding the
    // given callable as a command.
    template<typename Fn1, typename SS,
             typename Fn = typename std::decay<Fn1>::type>
    static constexpr auto ubound(SS ub0, Fn1 &&fn)
      -> decltype(
        ub0.template cat_size_of<executor_wire_t>()
           .template cat_ubound_of<Fn>(fn)
      ) {
      return ub0.template cat_size_of<executor_wire_t>()
                .template cat_ubound_of<Fn>(fn);
    }

    // Serialize the given callable and reader and cleanup actions onto the writer.
    template<detail::serialization_reader(*reader)(Arg...), void(*cleanup)(Arg...),
             typename Fn1, typename Writer,
             typename Fn = typename std::decay<Fn1>::type>
    static void serialize(Writer &w, std::size_t size_ub, Fn1 &&fn) {
      executor_wire_t exec = executor_wire_t(&the_executor<Fn,reader,cleanup>);
      
      w.template write<executor_wire_t>(exec);
      
      serialization_traits<Fn>::serialize(w, fn);
      
      UPCXX_ASSERT(
        size_ub == 0 || w.size() <= size_ub,
        "Overflowed serialization buffer: buffer="<<size_ub<<", packed="<<w.size()
      ); // blame serialization<Fn>::ubound()
    }
  };
}}
#endif
