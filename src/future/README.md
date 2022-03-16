# INTERNAL UPC++ MAINTAINER DOCUMENTATION #

This document covers internal design details of the UPC++ runtime and is not
intended for consumption by end users. In particular, all information herein is
non-normative, may be out-of-date and is subject to change without notice. End
users should NEVER rely on UPC++ types that are not documented in the formal
[UPC++ v1.0 Specification](https://bitbucket.org/berkeleylab/upcxx/raw/master/docs/spec.pdf)

## Implementation of `upcxx::future` ##

From reading our spec you might think `upcxx::future` is implemented with
something like:

```
// in namespace upcxx
template<typename ...T>
class future {
 /* oh you wish... */
};
```

But things are comically more elaborate than that. Instead what we have is
a small zoo of templated types similar to "c++ expression templates" (internet
searchable term). Rationale for this approach claims two massive advantages:

  1. Execution speed for expressions involving more than one future primitive.
  2. Synchronous execution across asynchronous abstraction boundaries when
     permissible (again for performance).

These are the basic ways you can construct futures.

```
future<T...> promise<T...>::get_future();

future<T...> upcxx::make_future(T...);

future<T> upcxx::to_future(T);
future<T> upcxx::to_future(future<T>);

future<(T...)...> upcxx::when_all(future<T...>...);

future<U> upcxx::future<T>::then(Callable<U(T&&)> &&fn);
```

The first, building from a promise, is the only way that builds a non-ready
future without having to provide a non-ready future as input. This means the
implementation cannot avoid allocating a dynamic object to track readiness.
The others, though, do not demand such runtime overhead in all contexts.
Obviously `make_future` never does, and `when_all` and `then` build immediately
ready futures if given ready futures. By using expression templates we can
track static knowledge of the futures (principally whether the future is
trivially ready) and use optimized representations to store them.

Non-exhaustive list of execution optimizations we would like to see:

```
make_future(val).then(fn);
// DONT: allocate ready future on heap
// DONT: allocate callback thunk on heap waiting for already ready future
// DONT: dispatch to callback through fnptr/virtual call
// DO: invoke immediately: "fn(val)" -- no heap, no virtual dispatch

when_all(make_future(1), ...);
// DONT: allocate ready future (make_future) with runtime dependency to
//       when_all node
// DO: embed value=1 into when_all node sans runtime dependency

when_all(make_future(1), make_future(2)).then(fn);
// DO: invoke immediately "fn(1,2)"
// Accomplished by composition of previous two points

(...).then(fn1).then(fn2);
// DONT: allocate two separate callback nodes
// DO: (...).then(/*magic composition of (fn1 o fn2)*/)
```

These optimizations are especially advantageous across the runtime/user
abstraction boundary. For instance `upcxx::rpc` takes a callable that returns a
future or non-future value (which in this case it implicitly promotes to a
future via make_future), and then fires back a reply when the returned user
future is readied. Thanks to these optimizations, if the user callback returns
a non-future, by the runtime internally then'ing the reply action onto the user
future which is now known to be immediately ready, the intermediate callback
heap allocation and virtual dispatch are entirely elided.

## How We Achieve This ##

First of all `future` isn't really a class but an alias like:

```
// namespace upcxx
namespace detail {
  template<typename Kind, typename ...T>
  class future1;

  using future_kind_default = ???;
}

template<typename ...T>
using future = detail::future1<detail::future_kind_default, T...>;
```

The `Kind` type parameter of `future1` encodes our AST expression using
templated "kind" classes. We generally have one for each of our future
producing primitives. The following is an approximation of the different
kinds:

```
namespace upcxx {
namespace detail {
  struct future_kind_result; // detail::make_fast_future result

  template<typename ...ArgFuture1s>
  struct future_kind_when_all; // detail::when_all_fast result

  // generic heaped refcounted runtime node. all other kinds can be cast to
  // this one through materialization to a runtime node.
  struct future_kind_shref;

  template<typename ArgKind, typename Fn>
  struct future_kind_then_lazy; // arg.then(fn) result
}}
```

As an example, here is a nested future expression with its full return type
spelled out:

```
expr: when_all_fast(make_future(1), when_all_fast(make_future(2), make_future(3)))
type: future1<
    /*Kind=*/future_kind_when_all<
      future1</*Kind=*/future_kind_result, /*T...=*/int>,
      future1<
        /*Kind=*/future_kind_when_all<
          future1</*Kind=*/future_kind_result, /*T...=*/int>,
          future1</*Kind=*/future_kind_result, /*T...=*/int>
        >,
        /*T...=*/int, int
      >
    >,
    /*T...=*/int, int, int
  >;
```

The user-facing primitives don't actually return these *spooky* types. They
cast to `future<T...>` with the default kind so as not to surprise users with
rare but inscrutable type deduction errors. But the runtime has internal
analogs for each user primitive that does return the spooky typed object, and
the runtime makes eager use of these operators to reap as much optimization
as possible. These analogs are:

```
upcxx::detail::make_fast_future; // semantic match for upcxx::make_future
upcxx::detail::when_all_fast; // match for upcxx::when_all
upcxx::detail::future1::then_lazy; // match for future1::then
```

See issue #288 for more about spooky futures getting into the user's hands.

## Abstraction Boundaries ##

A really nice thing about `future1` having separate `Kind` and `T` type
parameters is that it allows API's to demand type correctness for the semantic
features of the future (it's T types) while being parametric with non-semantic
Kind, e.g.:

```
// typechecks for any spooky future type but only if it carries the expected
// type "int"
template<typename Kind>
void gimme_int_someday(future1<Kind,int> eventual_number);
```

Generally you want to allow type deduction to preserve the future's type when
returning it from a function:

```
template<typename Kind>
auto chain_action_on_int(future1<Kind,int> input) {
 return input.then_lazy([](int val) { /*action*/; return val+1; });
}

// if called like so everything happens synchronously with no overhead:
chain_action_on_int(detail::make_fast_future(100))
  .then_lazy([](int val_plus_1) { /*another action*/; });
```


## Runtime Anatomy ##

For futures that must be materialized into a runtime node (we say "node" because
futures are a means to express graphs of dynamically triggered callbacks), the
structures used are as follows. First there is a heap allocated header
`detail::future_header` which contains:

  * a reference count
  * a status integer maintaining how many outstanding dependencies it has (number
    of non-ready futures its waiting for)
  * a pointer to another header (which could be itself) that holds or will hold
    the ready value.
  * a head-pointer to a list of successor headers, these are futures waiting for
    this (non-ready) header to ready.

`detail::future_header` is subclassed (without any polymorphism) to more
specialized header types depending what kind of future it is:

  * `detail::future_header_result<T...>`: adds result value storage.
  * `detail::future_header_promise<T...>`: adds result value storage and
    promise metadata.
  * `detail::future_header_dependent`: adds pointer to a `detail::future_body`.

`detail::future_body` base class for independently allocated polymorphic
callbacks that are to be executed once all dependencies are satisfied. Bodies
are allocated separately so that they can be deallocated immediately after
executing to free up that memory sooner than waiting for the header to be
dropped by the user.

`detail::future_body_proxy_` subclass of `detail::future_body`, maintains
body state for after a `then` callback body executes and has to wait on the
result future (it is *proxying* for another future).

I find it amazing that header inheritance can be done non-polymorphically. This
means we have to handle all the downcasting manually based on what we can infer
from context, but the benefit is no virtual dispatch. The _only_ virtual
dispatch that occurs is either once or twice per `then` callback; one if the
callback returns a ready future, two if it has to proxy.

`detail::future_dependency<Future1>`, specialized for all kinds of
`Future1 = future1<Kind,T...>`, holds the linked list linkage pointers for a
future body to sit in all successor lists for all of its future dependencies.
For instance, `future_dependency<future1<future_kind_shref>,T>` holds enough
linkage to sit in one future's successor list. A dependency on a
`future_kind_when_all` of two `future_kind_shref`'s holds two linkages. A
dependency on a `future_kind_result` holds no linkage, instead it stores the
`T` value directly thus avoiding any dynamic component for that dependency.

Hope this helps :)

