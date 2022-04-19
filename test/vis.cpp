#include <cstdint>
#include <vector>
#include <cstdlib>
#include <cstddef>
#include <iterator>

#include <upcxx/upcxx.hpp>

#include "util.hpp"

using namespace upcxx;
using namespace std;

#define M 68
#define N 128
#define B 20

typedef long long int lli;

typedef lli patch_t[M][N];

template<typename ptr_t, typename value_t=ptr_t>
class Iter 
{
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = value_t;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type*; 
  using reference = value_type&;

  Iter() = default;
  Iter(ptr_t a_p,  std::size_t a_size, int a_stride)
    :m_ptr(a_p),m_size(a_size), m_stride(a_stride) {}

  
  bool operator==(Iter rhs) const { return rhs.m_ptr==m_ptr;}
  bool operator!=(Iter rhs) const { return rhs.m_ptr!=m_ptr;}
  Iter& operator++(){m_ptr+=m_stride; return *this;}
  Iter  operator++(int){Iter rtn(*this); m_ptr+=m_stride; return rtn;} 

  void operator+=(std::ptrdiff_t a_skip) {m_ptr = m_ptr+a_skip;}

  std::ptrdiff_t operator-(const Iter& rhs) const {
    assert(m_stride==rhs.m_stride);
    return (m_ptr - rhs.m_ptr)/m_stride;
  }
  ptr_t m_ptr=0;
  std::size_t m_size=0;
  int   m_stride=0;
};


template<typename ptr_t>

class IterF: public Iter<ptr_t, std::pair<ptr_t,std::size_t>>
{
public:
  using Iter<ptr_t, std::pair<ptr_t,std::size_t>>::Iter;
  std::pair<ptr_t, std::size_t> operator*() const {
    return {this->m_ptr,this->m_size};
  }
};

template<typename ptr_t>
class IterR: public Iter<ptr_t>
{
public:
  using Iter<ptr_t>::Iter;
  IterR(ptr_t a_ptr, int a_stride) : Iter<ptr_t>(a_ptr,0,a_stride) { }
  ptr_t operator*() const {
    return this->m_ptr;
  }
};

template<typename Irreg, typename value_t>
bool check(Irreg start, Irreg end, value_t value);

template<typename Reg, typename value_t>
bool check(Reg start, Reg end, std::size_t count, value_t value, const char* name);

template<typename Reg, typename value_t>
void vset(Reg start, Reg end, std::size_t count, value_t value);

void reset(patch_t& patch, lli value);

lli sum(patch_t& patch);

typedef std::array<int,2> count_t;

int main() {

  bool success=true;
  
  init();
  {
  print_test_header();

  
  intrank_t me = rank_me();
  intrank_t n =  rank_n();
  intrank_t nebrHi = (me + 1) % n;
  intrank_t nebrLo = (me + n - 1) % n;

  // Ring of ghost halos transfered between adjacent ranks using the three
  // different communication protocols
  patch_t* myPatchPtr = (patch_t*)allocate(sizeof(patch_t));
  dist_object<global_ptr<lli> > mesh(to_global_ptr<lli>((lli*)myPatchPtr));
  dist_object<count_t>  counters(count_t({{0,0}}));

  count_t& mycount = *counters;// read from me, write to me
  lli*  myPtr = (*mesh).local();
  patch_t& myPatch = *myPatchPtr;

  future<global_ptr<lli>> fneighbor_hi = mesh.fetch(nebrHi);
  future<global_ptr<lli>> fneighbor_lo = mesh.fetch(nebrLo);

  reset(myPatch, me);
  

  global_ptr<lli> hi,lo;
  std::tie(hi,lo) = when_all(fneighbor_hi, fneighbor_lo).wait();
 
  // irregular test 1
  {
    lli srcTest[]= {me, me+1, me+2, me+3, me+4, me+5};
    std::vector<std::pair<lli*,std::size_t> > svec(1,{srcTest, 6});
    std::vector<std::pair<global_ptr<lli>, std::size_t> > dvec(1, {hi, 6});
    std::cout<<"\nsending to "<<hi.where()<<": ";
    for(int i=0; i<6; i++)
      std::cout<<" "<<srcTest[i];
    std::cout<<"\n";
    future<> fsource, foperation;
    std::tie(fsource, foperation)  = rput_irregular(svec.begin(), svec.end(),
                             dvec.begin(), dvec.end(),
                             source_cx::as_future() |
                             remote_cx::as_rpc([](dist_object<count_t>& c) {
                                 (*c)[1]++;}, counters) |
                             operation_cx::as_future()
                             );


    //mycount[0]++;  //reading from myself
    fsource.wait();
    srcTest[0]=-65; srcTest[5]=-2345653; //scramble some source entries.
    svec.clear();// invalidate iterator
    foperation.wait();

    while(mycount[1]!=1)  progress(); // wait to see my lo neighbor has modified my count
 
    for(int i=0; i<6; i++)
      {
        if(myPtr[i] != nebrLo+i){
          std::cout<<" simple sequence Irregular expected "<< nebrLo+i<<" but got "<<myPtr[i]<<"\n";
          success=false;
        }
      }
  }
  // irregular put test 2
  std::cout<<"\nIrregular test 2 \n";
  auto fs1 = IterF<lli*>(myPtr+N-B, B, N);
  auto fs1_end = fs1; fs1_end+=M*N;
  auto fd1 = IterF<global_ptr<lli>>(hi, B, N);
  auto fd1_end = fd1; fd1_end+=M*N;
  auto fr1 = IterF<lli*>(myPtr, B, N);
  auto fr1_end = fr1; fr1_end+=M*N;
  auto rr1 = IterR<lli*>(myPtr+N-B, N);
  auto rr1_end = rr1;  rr1_end+=M*N;
  
  barrier(); // need to see the reset done above
  future<> fsource, foperation;
  std::tie(fsource, foperation) = rput_irregular(fs1, fs1_end, fd1, fd1_end,
                           source_cx::as_future()| operation_cx::as_future());


 
  fsource.wait();
  // clobber source data locations now, verify source completion is OK
  // with data being altered after source completion.
  lli token = -15;

  vset(rr1, rr1_end, B, token);
  foperation.wait();

  barrier();
  
  success = success && check(fr1, fr1_end, (lli)nebrLo);
  lli sm = sum(myPatch);
  lli correctAnswer = (token*B+me*(N-2*B)+B*nebrLo)*M;
  
  if(sm != correctAnswer)
    {
      std::cout<<" Irregular expected sum:"<<correctAnswer<<" actual sum: "<<sm<<"\n";
      success = false;
    }

  reset(myPatch, me);
  std::cout<<"\nRegular put test 1\n";
  auto rs1 = IterR<lli*>(myPtr,N);
  auto rs1_end = rs1; rs1_end+=M*N;
  auto rd1 = IterR<global_ptr<lli>>(lo+N-B,N);
  auto rd1_end = rd1; rd1_end+=M*N;

  token=-1;
  vset(rr1, rr1_end, B, token);
  
  barrier();  
 
  std::tie(fsource,foperation) = rput_regular(rs1, rs1_end, B , rd1, rd1_end, B,
                                 source_cx::as_future()|operation_cx::as_future());
  fsource.wait();
  vset(rs1, rs1_end, B, token); // clobber source data locations
  
  auto r1_empty = rput_regular(rs1, rs1, B, rd1, rd1, B,
                               source_cx::as_lpc(default_persona(),[&](){ mycount[0]++;}) |
                               operation_cx::as_future());
  r1_empty.wait();
  foperation.wait();
  barrier();
  
  success = success && check(rr1, rr1_end, B, (lli)nebrHi, "Regular");
  sm = sum(myPatch);
  correctAnswer = (token*B+me*(N-2*B)+B*nebrHi)*M;
  if(sm != correctAnswer)
    {
      std::cout<<" Regular expected sum:"<<correctAnswer<<" actual sum: "<<sm<<"\n";
      success = false;
    }
 
  
  reset(myPatch, me);
  barrier();
  auto s1 = IterR<lli*>(myPtr,N);
  auto s1_end = s1; s1_end+=M*N;
  auto c1 = IterR<lli*>(myPtr+N-B,N);
  auto c1_end = c1; c1_end+=M*N;
 
  // strided put
  std::tie(fsource,foperation) = rput_strided<2>(myPtr+N-B, {{sizeof(lli),N*sizeof(lli)}},
                            hi, {{sizeof(lli),N*sizeof(lli)}}, {{B,M}},
                            source_cx::as_lpc(default_persona(),[&](){ mycount[0]++;}) |
                                                 source_cx::as_future() |                     
                            operation_cx::as_future() |
                            remote_cx::as_rpc([](dist_object<count_t>& c){
                                (*c)[1]++;},counters));

  fsource.wait();
  vset(c1, c1_end, B, token); // clobber source data locations
  foperation.wait();
  while(mycount[0] !=2 || mycount[1] != 2) progress();
  // mycount[1] == 2 means that the rput to me should be completed
  // seeing mycount[0] == 2 means that the source_cx::as_lpc has fired
  
  std::cout<<"\nStrided put testing \n";
  success = success && check(s1, s1_end, B, (lli)nebrLo, "Strided"); // this is moving the same data as in the irregular case
  sm = sum(myPatch);
  correctAnswer = (token*B + me*(N-2*B)+B*nebrLo)*M;

  if(sm != correctAnswer)
    {
      std::cout<<" Stride expected sum:"<<correctAnswer<<" actual sum: "<<sm<<"\n";
      success = false;
    }

  
  //   VIS rget tests
  
      // irregular get test 1
  std::cout<<"Irregular rget test 1\n";
  lli m=me;
  lli getTest[]= {-m, -m-1, -m-2, -m-3, -m-4, -m-5};
  for(int i=0; i<6; i++)
    {
      myPtr[i]=m+i;
    }
  std::vector<std::pair<lli*,std::size_t> > dvec(1,{getTest, 6});
  std::vector<std::pair<global_ptr<lli>, std::size_t> > svec(1, {hi, 6});
  std::cout<<"\nleaving for "<<hi.where()<<": ";
  for(int i=0; i<6; i++)
    std::cout<<" "<<myPtr[i];
  std::cout<<"\n";
  barrier();
  auto fg0 = rget_irregular(svec.begin(), svec.end(), dvec.begin(), dvec.end(),
                            operation_cx::as_lpc(default_persona(),[&](){ mycount[0]++;}) |
                            operation_cx::as_future() |
                            remote_cx::as_rpc([](dist_object<count_t>& c){
                                (*c)[1]++;},counters)
                            );

  fg0.wait();
  
  for(int i=0; i<6; i++)
    {
      if(getTest[i] != nebrHi+i){
        std::cout<<" simple sequence Irregular get expected "<< nebrHi+i<<" but got "<<getTest[i]<<"\n";
        success=false;
      }
    }

  while(mycount[0] != 3 || mycount[1] != 3) progress(); // ensure that every executes lpc and rpc completions
  barrier();
 
  
  reset(myPatch, me);
  std::cout<<"Irregular rget test 2\n";
  barrier();
  
  auto fg2 = rget_irregular(fd1, fd1_end, fs1, fs1_end);

  fr1 = IterF<lli*>(myPtr+N-B, B, N);
  fr1_end = fr1; fr1_end+=M*N;
  
  fg2.wait();

  success = success && check(fr1, fr1_end, (lli)nebrHi);
  sm = sum(myPatch);
  correctAnswer = (me*(N-B)+B*nebrHi)*M;
  if(sm != correctAnswer)
    {
      std::cout<<" Irregular get expected sum:"<<correctAnswer<<" actual sum: "<<sm<<"\n";
      success = false;
    }

  barrier();
  reset(myPatch, me);
  rr1 = IterR<lli*>(myPtr, N);
  rr1_end = rr1; rr1_end+=M*N;
  vset(rr1, rr1_end, B, token);
  barrier();
  auto g1 = rget_regular(rd1, rd1_end, B, rs1, rs1_end, B);
  g1.wait();
  sm=sum(myPatch);
  success = success && check(rr1, rr1_end, B, (lli)nebrLo, "Regular");
  correctAnswer = (me*(N-B)+B*nebrLo)*M;
  if(sm != correctAnswer)
    {
      std::cout<<" Regular get expected sum:"<<correctAnswer<<" actual sum: "<<sm<<"\n";
      success = false;
    }

  barrier();
  std::cout<<"rget_strided test 1\n";
  reset(myPatch, me);

  barrier();
  //  a bit fancier than the rput,  this one gets every other row in the source patch buffer
  //  and inserts it with a one row offset into myPatch
  auto sg1 = rget_strided<2>(lo+N-B, {{sizeof(lli), 2*N*sizeof(lli)}},
                             myPtr+N, {{sizeof(lli), 2*N*sizeof(lli)}}, {{B,M/2}});
 
  sg1.wait();
  sm=sum(myPatch);
  correctAnswer = B*M/2*me + B*M/2*nebrLo + (N-B)*M*me;

  if(sm != correctAnswer)
    {
      std::cout<<" Strided get expected sum:"<<correctAnswer<<" actual sum: "<<sm<<"\n";
      success = false;
    }


  //  Transpose strided operations
  std::cout<<"rput_strided with transpose\n";

  uint8_t guardCellHi=0xFF;
  lli tmp[5][2] = {{m,m+1},{m+2, m+3},{m+4,m+5},{m+6,m+7},{m+8,m+9}};
  uint8_t guardCellLo=0xFF;
  
  reset(myPatch, me);
  barrier();
  //  insert the transpose of tmp into the start of hi patch
  auto tr1 = rput_strided<2>(&(tmp[0][0]), {{sizeof(lli),2*sizeof(lli)}},
                            hi, {{N*sizeof(lli),sizeof(lli)}}, {{2,5}});
  tr1.wait();
  for(lli* s=&(tmp[0][0]); s<&(tmp[0][0])+10; s++) *s=0;
  barrier();
  sm=sum(myPatch);
  correctAnswer = me*(N*M-10) + 10*nebrLo + 45;
  if(sm != correctAnswer)
    {
      std::cout<<" Strided transpose put expected sum:"<<correctAnswer<<" actual sum: "<<sm<<"\n";
      success = false;
    }
  lli val=nebrLo;
  for(int i=0;i<5;i++)
    for(int j=0;j<2; j++)
      {
        if(myPatch[j][i] != val){
          std::cout<<"Wrong value in transpose "<<myPatch[j][i]<<" "<<val<<"\n";
          success=false;
        }
        myPatch[j][i]+=1+m; // set up the get test next;
        val++;
      }

  barrier();

  std::cout<<"rget_strided with transpose\n";
  // slightly different arrangement, unit stride at the remote rank, jumbled
  // stride on arrival buffer.
  auto tr2 = rget_strided<2>(hi, {{sizeof(lli),N*sizeof(lli)}},
                             &(tmp[0][0]), {{2*sizeof(lli),sizeof(lli)}},
                             {{5,2}});

  tr2.wait();

  if(guardCellHi != 0xFF || guardCellLo != 0xFF)
    {
      std::cout<<"guardCells corrupted\n";
      success=false;
    }

  val = m + nebrHi+1;
  for(int j=0; j<5; j++)
    for(int i=0; i<2; i++)
      {
        if(tmp[j][i] != val)
          {
          std::cout<<"rget_strided transpose got:"<<tmp[j][i]<<" expected"<<val<<"\n";
          success=false;
          }
        val+=1;
      }
  
  print_test_success(success);
  
  }
  finalize();

  if(!success) return 4;
  return 0;
}

// support functions

void reset(patch_t& patch, lli value)
{
  for(int j=0; j<M; j++)
    {
      for(int i=0; i<N; i++)
        {
          patch[j][i]=  value;
        }
    }
}

lli sum(patch_t& patch)
{
  lli rtn=0;
  for(int j=0; j<M; j++)
    {
      lli jsum=0;
      for(int i=0; i<N; i++)
        {
          jsum+=patch[j][i];
        }
      rtn+=jsum;
    }
  return rtn;
}


template<typename Irreg, typename value_t>
bool check(Irreg start, Irreg end, value_t value)
{
  while(!(start == end))
    {
      value_t* v = std::get<0>(*start);
      value_t* e = v + std::get<1>(*start);
      for(;v<e; ++v)
        {
          if(*v != value)
            {
              std::cout<<"Irregular expected value:"<<value<<" seeing:"<<*v;
              return false;
            }
        }
      ++start;
    }
  return true;
}


template<typename Reg, typename value_t>
bool check(Reg start, Reg end, std::size_t count, value_t value, const char* name)
{
  while(!(start == end))
    {
      value_t* v = *start;
      value_t* e = v+count;
      for(;v!=e;++v)
        {
          if(*v != value)
            {
              std::cout<<name <<" expected value:"<<value<<" seeing:"<<*v;
              return false;
            }
        }
      ++start;
    }
  return true;
}

template<typename Reg, typename value_t>
void vset(Reg start, Reg end, std::size_t count, value_t value)
{
  while(!(start == end))
    {
      value_t* v = *start;
      value_t* e = v+count;
      for(;v!=e;++v)
        {
          *v=value;
        }
      ++start;
    }
}
        
