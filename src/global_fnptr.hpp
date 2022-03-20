#ifndef _9c3b2cb6_d978_4c8d_9b3e_a077c8926dfa
#define _9c3b2cb6_d978_4c8d_9b3e_a077c8926dfa

#include <upcxx/diagnostic.hpp>
#include <upcxx/ccs.hpp>

#include <cstdint>
#include <cstring>
#include <functional>

namespace upcxx {
  template<typename T>
  struct serialization;

namespace detail {
  //////////////////////////////////////////////////////////////////////////////
  // global_fnptr<Ret(Arg...)>: shippable pointer-to-function
  template<typename FnSig, typename FunctionToken = detail::FunctionTokenType>
  class global_fnptr;

  template<typename ...Arg>
  class command; // defined in command.hpp

  template<typename Ret, typename ...Arg, typename FunctionToken>
  class global_fnptr<Ret(Arg...), FunctionToken> {
    static_assert(
      sizeof(Ret(*)(Arg...)) == sizeof(std::uintptr_t),
      "Function pointers must be the same size as regular pointers."
    );

  public:
    using function_type = Ret(Arg...);

    friend struct std::hash<upcxx::detail::global_fnptr<Ret(Arg...),FunctionToken>>;
    friend class detail::command<FunctionTokenType, Arg...>;
    friend struct serialization<global_fnptr>;

  private:
    constexpr global_fnptr(const FunctionToken& ft) : u_(ft) {}
    constexpr global_fnptr(FunctionToken&& ft) : u_(std::move(ft)) {}
    FunctionToken u_;

  public:
    constexpr global_fnptr(std::nullptr_t null = nullptr): u_{} {}

    //global_fnptr(Ret(&fn)(Arg...)): u_{encode(&fn)} {}
    global_fnptr(Ret(*fp)(Arg...)): u_(decltype(u_)::tokenize(fp)) {}

    template<typename... Args2>
    Ret operator()(Args2&& ...a) const {
      return u_.template detokenize<typename std::add_pointer<function_type>::type>()(std::forward<Args2>(a)...);
    }

    //constexpr operator bool() const { return u_ != detail::global_fnptr_null; }
    //constexpr bool operator!() const { return u_ == detail::global_fnptr_null; }

    inline typename std::add_pointer<function_type>::type detokenize() const {
      return u_.template detokenize<typename std::add_pointer<function_type>::type>();
    }

    friend struct std::hash<global_fnptr<Ret(Arg...),FunctionToken>>;

    friend constexpr bool operator==(global_fnptr<Ret(Arg...),FunctionToken> a, global_fnptr<Ret(Arg...),FunctionToken> b) {
      return a.u_ == b.u_;
    }
    friend constexpr bool operator!=(global_fnptr<Ret(Arg...),FunctionToken> a, global_fnptr<Ret(Arg...),FunctionToken> b) {
      return a.u_ != b.u_;
    }
    friend constexpr bool operator<(global_fnptr<Ret(Arg...),FunctionToken> a, global_fnptr<Ret(Arg...),FunctionToken> b) {
      return a.u_ < b.u_;
    }
    friend constexpr bool operator<=(global_fnptr<Ret(Arg...),FunctionToken> a, global_fnptr<Ret(Arg...),FunctionToken> b) {
      return a.u_ <= b.u_;
    }
    friend constexpr bool operator>(global_fnptr<Ret(Arg...),FunctionToken> a, global_fnptr<Ret(Arg...),FunctionToken> b) {
      return a.u_ > b.u_;
    }
    friend constexpr bool operator>=(global_fnptr<Ret(Arg...),FunctionToken> a, global_fnptr<Ret(Arg...),FunctionToken> b) {
      return a.u_ >= b.u_;
    }
  };

    ////////////////////////////////////////////////////////////////////////////
    // detail::globalize_fnptr: Given a callable, return a global_fnptr if that
    // callable is a function pointer/reference, otherwise return the given
    // callable unaltered.

    template<typename Fn>
    Fn&& globalize_fnptr(Fn &&fn) {
      return std::forward<Fn>(fn);
    }
    template<typename Ret, typename ...Arg>
    global_fnptr<Ret(Arg...)> globalize_fnptr(Ret(*fn)(Arg...)) {
      return global_fnptr<Ret(Arg...)>(fn);
    }

    template<typename Fn, typename Fn1 = typename std::decay<Fn>::type>
    struct globalize_fnptr_return {
      using type = Fn;
    };
    template<typename Fn, typename Ret, typename ...Arg>
    struct globalize_fnptr_return<Fn, Ret(*)(Arg...)> {
      using type = global_fnptr<Ret(Arg...)>;
    };
    template<typename Fn, typename Ret, typename ...Arg>
    struct globalize_fnptr_return<Fn, Ret(&)(Arg...)> {
      using type = global_fnptr<Ret(Arg...)>;
    };
  }

  template<typename Fn>
  struct serialization<detail::global_fnptr<Fn,detail::function_token>> {

    // The MSB is used to encode the function token type. Because non-legacy relocations use the start of the segment as the basis, the offset should never be negative.
    static constexpr uintptr_t msb = 1ull << (std::numeric_limits<uintptr_t>::digits-1);

    template<typename Writer>
    static void serialize(Writer& w, const detail::global_fnptr<Fn,detail::function_token>& gfnptr)
    {
      if (gfnptr.u_.token_ident() == detail::function_token::identifier::single) {
        const auto& ss = gfnptr.u_.template get<detail::function_token_ss>();
        UPCXX_ASSERT((ss.offset & msb) == 0);
        w.write_trivial(ss.offset);
      } else {
        const auto& ms = gfnptr.u_.template get<detail::function_token_ms>();
        UPCXX_ASSERT((ms.offset & msb) == 0);
        w.write_trivial(ms.offset | msb);
        w.write_trivial(ms.ident);
      }
    }

    template<typename Reader>
    static detail::global_fnptr<Fn,detail::function_token>* deserialize(Reader& r, void* storage)
    {
      auto offset = r.template read_trivial<uintptr_t>();
      if (!(offset & msb)) {
        return ::new(storage) detail::global_fnptr<Fn,detail::function_token>{detail::function_token_ss{offset}};
      } else {
        return ::new(storage) detail::global_fnptr<Fn,detail::function_token>{detail::function_token_ms{offset ^ msb, r.template read_trivial<detail::segment_hash>()}};
      }
    }
  };
}

namespace std {
  template<typename FunctionToken, typename Ret, typename ...Arg>
  struct hash<upcxx::detail::global_fnptr<Ret(Arg...), FunctionToken>> {
    constexpr std::size_t operator()(upcxx::detail::global_fnptr<Ret(Arg...), FunctionToken> x) const {
      return std::hash<decltype(x.u_)>{}(x.u_);
    }
  };
}
#endif
