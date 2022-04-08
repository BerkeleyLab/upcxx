#ifndef _4235e9ca_a704_450e_b2e1_0526cbd806ae
#define _4235e9ca_a704_450e_b2e1_0526cbd806ae

#include "row.hpp"
#include "os_env.hpp"

#include <upcxx/upcxx.hpp>

#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

template<typename=void>
void print_test_skipped(const char *reason, const char *success_msg="SUCCESS") {
  bool speak = true;
  if (upcxx::initialized()) {
     upcxx::barrier();
     if (upcxx::rank_me()) speak = false;
  }
  if (speak) std::cout
        << "Test result: "<< "SKIPPED" << "\n"
        << "UPCXX_TEST_SKIPPED: This test was skipped due to: " << reason << "\n"
        << "Please ignore the following line which placates our automated test infrastructure:\n"
        << success_msg << "\n";
  if (upcxx::initialized()) upcxx::barrier();
}


namespace bench {
  // Writes a report file consisting of emitted rows. This may not be entered
  // concurrently, so you will need to funnel your report data to a single rank
  // to write the report. Constructor reads env vars:
  //  "report_file": location to append data points, special value "-" indicates
  //    stdout (default="report.out").
  //  "report_args": python formatted string of keyword argument assignments
  //    to be passed as additional independent variable assignments to the `emit`
  //    function.
  class report {
    std::string args, app, filename;
    std::ostream *f;
    
  public:
    report(const char *appstr/* = typically pass __FILE__*/);
    ~report();
    
    void blank() {
      *f << std::endl;
    }
    
    // Given a list of dependent variable names and a row, emit a line data-point line.
    template<typename ...T>
    void emit(std::initializer_list<char const*> dependent_vars, row<T...> const &x) {
      *f << "emit([";
      for(auto dep: dependent_vars)
        *f << '"' << dep << "\",";
      *f << ']';
      *f << ", app=\"" << app << '"';
      x.print_name_eq_val(*f, true);
      *f << ',' << args << ")" << std::endl;
    }
  };
  
  //////////////////////////////////////////////////////////////////////////////
  
  inline report::report(const char *appstr) {
    app = std::string(appstr);
    
    std::size_t pos = app.rfind("/");
    pos = pos == std::string::npos ? 0 : pos+1;
    app = app.substr(pos);
    
    if(app.size() > 4 && app.substr(app.size()-4, 4) == ".cpp")
      app = app.substr(0, app.size()-4);
    
    args = os_env<std::string>("report_args", "");
    filename = os_env<std::string>("report_file", "report.out");
    
    args = "ranks=" + std::to_string(upcxx::rank_n()) + "," + args;

    if(filename == "-")
      f = &std::cout;
    else {
      f = new std::ofstream(filename, std::ofstream::app);
      
      // write the current time as a comment
      std::time_t t = std::time(0);
      std::tm now = *std::localtime(&t);
      char buf[128];
      std::strftime(buf, sizeof(buf), "%Y-%m-%d %X", &now);
      *f << "# Opened for append at "<<buf<<std::endl;
    }
  }

  inline report::~report() {
    if(f != &std::cout) {
      std::cerr << "Report written to '" << filename << "'." << std::endl;
      delete f;
    }
  }
}

#endif
