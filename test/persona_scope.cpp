#include <upcxx/upcxx.hpp>
#include "util.hpp"

#include <mutex>

using namespace upcxx;

#define CHECK UPCXX_ASSERT_ALWAYS

struct counting_mutex : public std::recursive_mutex {
 private:
  int count = 0;
 public:
  int get_count() { return count; }
  void lock() {
    std::recursive_mutex::lock();
    count++;
  }
  bool try_lock() {
    bool result = std::recursive_mutex::try_lock();
    if (result) count++;
    return result;
  }
  void unlock() {
    count--;
    std::recursive_mutex::unlock();
  }
};

void check_progress() {
  auto check_req = []() {
    CHECK(!progress_required());
    CHECK(!progress_required(default_persona_scope()));
    CHECK(!progress_required(top_persona_scope()));
  };
  check_req();
  progress();
  check_req();
  progress(progress_level::internal);
  check_req();
  discharge();
  discharge(default_persona_scope());
  discharge(top_persona_scope());
  check_req();
}

persona s_persona; // static persona
persona *my_persona_p = nullptr;

void check_active(bool have_my = false, bool have_master = false, bool have_s = false) {
    auto check_awc = [=]() {
      CHECK(default_persona().active_with_caller());
      CHECK(have_my ==     my_persona_p->active_with_caller());
      CHECK(have_master == master_persona().active_with_caller());
      CHECK(have_s ==      s_persona.active_with_caller());
    };
    check_awc();
    check_progress();
    check_awc();
}

void check(persona &base, persona_scope &base_scope) {
  
  persona my_persona;  // auto persona
  my_persona_p = &my_persona;

  CHECK(&current_persona() == &base);
  CHECK(&top_persona_scope() == &base_scope);

  check_active(false);

  // test simple push and pop

  { // push my_persona
    persona_scope my_scope(my_persona);

    CHECK(&top_persona_scope() == &my_scope);
    CHECK(&current_persona() == &my_persona);
    check_active(true);
  } // pop my_persona

  CHECK(&current_persona() == &base);
  CHECK(&top_persona_scope() == &base_scope);
  check_active(false);

  { // push s_persona
    persona_scope my_scope(s_persona);

    CHECK(&top_persona_scope() == &my_scope);
    CHECK(&current_persona() == &s_persona);
    check_active(false, false, true);
  } // pop s_persona

  CHECK(&current_persona() == &base);
  CHECK(&top_persona_scope() == &base_scope);
  check_active(false);

  // test 2-level redundant push and pop

  { // push my_persona
    persona_scope my_scope(my_persona);
    
    CHECK(&top_persona_scope() == &my_scope);
    CHECK(&current_persona() == &my_persona);
    check_active(true);

    { // push my_persona again in scope2
      persona_scope my_scope2(my_persona);

      CHECK(&top_persona_scope() == &my_scope2);
      CHECK(&current_persona() == &my_persona);
      check_active(true);
    } // pop scope2
    
    CHECK(&top_persona_scope() == &my_scope);
    CHECK(&current_persona() == &my_persona);
    check_active(true);
  } // pop my_persona

  CHECK(&current_persona() == &base);
  CHECK(&top_persona_scope() == &base_scope);
  check_active(false);

  // test multi-level redundant push and pop

  { // push my_persona
    persona_scope my_scope(my_persona);
    
    CHECK(&top_persona_scope() == &my_scope);
    CHECK(&current_persona() == &my_persona);
    check_active(true);

    { // push master_persona in scope2
      persona_scope my_scope2(master_persona());

      CHECK(&top_persona_scope() == &my_scope2);
      CHECK(&current_persona() == &master_persona());
      check_active(true, true);

      { // push default_persona again in scope3
        persona_scope my_scope3(default_persona());

        CHECK(&top_persona_scope() == &my_scope3);
        CHECK(&current_persona() == &default_persona());
        check_active(true, true);

        { // push my_persona again in scope4
          persona_scope my_scope4(my_persona);

          CHECK(&top_persona_scope() == &my_scope4);
          CHECK(&current_persona() == &my_persona);
          check_active(true, true);

          { // push s_persona in scope5
            persona_scope my_scope5(s_persona);

            CHECK(&top_persona_scope() == &my_scope5);
            CHECK(&current_persona() == &s_persona);
            check_active(true, true, true);

            { // push master_persona again in scope6
              persona_scope my_scope6(master_persona());

              CHECK(&top_persona_scope() == &my_scope6);
              CHECK(&current_persona() == &master_persona());
              check_active(true, true, true);

            } // pop scope6

            CHECK(&top_persona_scope() == &my_scope5);
            CHECK(&current_persona() == &s_persona);
            check_active(true, true, true);

          } // pop scope5

          CHECK(&top_persona_scope() == &my_scope4);
          CHECK(&current_persona() == &my_persona);
          check_active(true, true);

        } // pop scope4

        CHECK(&top_persona_scope() == &my_scope3);
        CHECK(&current_persona() == &default_persona());
        check_active(true, true);

      } // pop scope3

      CHECK(&top_persona_scope() == &my_scope2);
      CHECK(&current_persona() == &master_persona());
      check_active(true, true);

    } // pop scope2

    CHECK(&top_persona_scope() == &my_scope);
    CHECK(&current_persona() == &my_persona);
    check_active(true);
  } // pop my_persona

  CHECK(&top_persona_scope() == &base_scope);
  CHECK(&current_persona() == &base);
  check_active(false);

  // test counting_mutex
  
  counting_mutex lock;
  CHECK(lock.get_count() == 0);
  {  std::lock_guard<counting_mutex> g(lock);
     CHECK(lock.get_count() == 1);
  }
  CHECK(lock.get_count() == 0);

  // test simple locked push and pop
  
  { // push my_persona
    persona_scope my_scope(lock,my_persona);
   
    CHECK(lock.get_count() == 1);
    CHECK(&current_persona() == &my_persona);
    check_active(true);
  } // pop my_persona

  CHECK(lock.get_count() == 0);
  CHECK(&current_persona() == &base);
  CHECK(&top_persona_scope() == &base_scope);
  check_active(false);

  // test multi-level redundant locked push and pop
  
  { // push my_persona
    persona_scope my_scope(lock,my_persona);
    
    CHECK(lock.get_count() == 1);
    CHECK(&top_persona_scope() == &my_scope);
    CHECK(&current_persona() == &my_persona);
    check_active(true);

    { // push master_persona in scope2
      persona_scope my_scope2(lock,master_persona());

      CHECK(lock.get_count() == 2);
      CHECK(&top_persona_scope() == &my_scope2);
      CHECK(&current_persona() == &master_persona());
      check_active(true, true);

      { // push default_persona again in scope3
        persona_scope my_scope3(lock,default_persona());

        CHECK(lock.get_count() == 3);
        CHECK(&top_persona_scope() == &my_scope3);
        CHECK(&current_persona() == &default_persona());
        check_active(true, true);

        { // push my_persona again in scope4
          persona_scope my_scope4(lock,my_persona);

          CHECK(lock.get_count() == 4);
          CHECK(&top_persona_scope() == &my_scope4);
          CHECK(&current_persona() == &my_persona);
          check_active(true, true);

          { // push s_persona in scope5
            persona_scope my_scope5(lock, s_persona);

            CHECK(lock.get_count() == 5);
            CHECK(&top_persona_scope() == &my_scope5);
            CHECK(&current_persona() == &s_persona);
            check_active(true, true, true);

            { // push master_persona again (unlocked) in scope6
              persona_scope my_scope6(master_persona());

              CHECK(lock.get_count() == 5);
              CHECK(&top_persona_scope() == &my_scope6);
              CHECK(&current_persona() == &master_persona());
              check_active(true, true, true);

            } // pop scope6

            CHECK(lock.get_count() == 5);
            CHECK(&top_persona_scope() == &my_scope5);
            CHECK(&current_persona() == &s_persona);
            check_active(true, true, true);

          } // pop scope5

          CHECK(lock.get_count() == 4);
          CHECK(&top_persona_scope() == &my_scope4);
          CHECK(&current_persona() == &my_persona);
          check_active(true, true);

        } // pop scope4

        CHECK(lock.get_count() == 3);
        CHECK(&top_persona_scope() == &my_scope3);
        CHECK(&current_persona() == &default_persona());
        check_active(true, true);

      } // pop scope3

      CHECK(lock.get_count() == 2);
      CHECK(&top_persona_scope() == &my_scope2);
      CHECK(&current_persona() == &master_persona());
      check_active(true, true);

    } // pop scope2

    CHECK(lock.get_count() == 1);
    CHECK(&top_persona_scope() == &my_scope);
    CHECK(&current_persona() == &my_persona);
    check_active(true);
  } // pop my_persona

  CHECK(lock.get_count() == 0);
  CHECK(&top_persona_scope() == &base_scope);
  CHECK(&current_persona() == &base);
  check_active(false);
}


int main() {
  init();

  print_test_header();

  liberate_master_persona();

  // check with the minimal stack
  check(default_persona(), default_persona_scope());

  { persona outer_persona;
    persona_scope outer_scope(outer_persona);

    // check after pushing a new persona
    check(outer_persona, outer_scope);

    { persona_scope def_scope(default_persona()); 
      // check after redundant push of default_persona
      check(default_persona(), def_scope);
    }

    // recheck
    check(outer_persona, outer_scope);
     
    // exit time
    { persona_scope scope(master_persona());
      print_test_success(true);

      finalize();
    }
  } // deliberate post-finalize destruction of outer*
  
  return 0; 
}
