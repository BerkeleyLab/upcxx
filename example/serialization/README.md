# Serialization Examples

This folder contains several examples of using the UPC++ Serialization APIs. An
example Makefile is provided that can be used to compile these examples.

UPC++ Serialization provides several different mechanisms for specifying how
serialization and deserialization of some user-defined class `T` are to be performed:

1. Declare which member variables of `T` to serialize with `UPCXX_SERIALIZED_FIELDS`.
   UPC++ automatically generates the required serialization logic.
2. Specify expressions for computing the data to be serialized with
   `UPCXX_SERIALIZED_VALUES`. UPC++ automatically generates logic to evaluate
   the expressions in serialization and invoke a constructor with the resulting values in
   deserialization.
3. Define a public, nested `T::upcxx_serialization` member type with public
   serialize and deserialize member-function templates.
4. Define a specialization of `upcxx::serialization<T>` with public serialize and
   deserialize member-function templates.

The examples in this folder illustrate each of these methods. A brief
description of each example is included below:

1. [upcxx_serialized_fields.cpp](./upcxx_serialized_fields.cpp): Illustrates how to use
   `UPCXX_SERIALIZED_FIELDS` to serialize a user-defined class by selecting the
   set of fields to be serialized, by name.
2. [upcxx_serialized_values.cpp](./upcxx_serialized_values.cpp): Illustrates how to use
   `UPCXX_SERIALIZED_VALUES` to serialize a user-defined class by providing a
   set of expressions whose results should be serialized.
3. [upcxx_serialization.cpp](upcxx_serialization.cpp): Illustrates how to use either (1) a nested
   `upcxx_serialization` member type, or (2) a specialization of
   `upcxx::serialization` to implement custom serialization. In both cases, the
   programmer must implement a `serialize` method that takes a `Writer` object
   and a `deserialize` method that takes a `Reader` object.
5. [upcxx_serialized_fields_recursive.cpp](upcxx_serialized_fields_recursive.cpp): Illustrates recursive serialization
   using `UPCXX_SERIALIZED_FIELDS` on nested STL and user-defined classes.
6. [upcxx_serialized_values_recursive.cpp](upcxx_serialized_values_recursive.cpp): Illustrates recursive serialization
   using `UPCXX_SERIALIZED_VALUES` on nested STL and user-defined classes.
7. [serialize_abstract_base.cpp](serialize_abstract_base.cpp): Demonstrates custom serialization
   on classes with abstract base classes.
8. [unrolled_list.cpp](unrolled_list.cpp): Illustrates custom serialization for
   an "unrolled linked list" data structure.

You must have at least UPC++ version 2022.9.0 installed to compile these
examples. To build all code, make sure to first set the `UPCXX_INSTALL`
variable. e.g. 

`export UPCXX_INSTALL=<installdir>`

then

`make all`

Run these examples as usual, e.g. 

`upcxx-run -n 4 ./upcxx_serialization`
