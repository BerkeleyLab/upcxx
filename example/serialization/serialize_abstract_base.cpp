#include <upcxx/upcxx.hpp>
#include <assert.h>
#include <iostream>

// this example code demonstrates how to serialize a class that has an 
// abstract base class, using Custom serialization.
//
// Abstract classes cannot be instantiated, so they cannot themselves be
// Serializable - this means they cannot participate directly as an independent
// entity in a serialization hierarchy. However their concrete derived classes
// CAN serialize the contents of an abstract base class, making the derived
// type(s) Serializable.
//

class A { // abstract base class
    public: 
    int stubint;
    bool stubbool;

    virtual void act() = 0;
    virtual ~A() = default;

    protected:
    // create some helpers to (de)serialize the state in this abstract class
    // we call these explicitly from the serialization hooks in derived classes
    template<typename Writer>
    void serialize_helper(Writer& writer) const {
      writer.write(stubint);
      writer.write(stubbool);
    }

    template<typename Reader>
    void deserialize_helper(Reader& reader) {
       stubint = reader.template read<int>();
       stubbool = reader.template read<bool>();
    }
};


class B : public A {
    public:
    int anotherstub;

    void act(){
        //do something here
        return;
    }
    
    struct upcxx_serialization{
        template<typename Writer>
        static void serialize (Writer& writer, B const & object){
            object.serialize_helper(writer); // serialize base state
            writer.write(object.anotherstub);
        }

        template<typename Reader, typename Storage>
        static B* deserialize(Reader& reader, Storage storage){
            B *b = storage.construct(); // create B object in storage
            b->deserialize_helper(reader); // deserialize base state
            b->anotherstub = reader.template read<int>();
            return b;
        }
    };
};

int main() {
  upcxx::init();
 
  B myb;
  myb.stubint = 42;
  myb.stubbool = true;
  myb.anotherstub = 666;

  upcxx::rpc(0, [](B const &b) {
      assert(b.stubint == 42);
      assert(b.stubbool == true);
      assert(b.anotherstub == 666);
      }, myb).wait();

  upcxx::barrier();
  if (!upcxx::rank_me()) std::cout << "SUCCESS" << std::endl;
  upcxx::finalize();
}
