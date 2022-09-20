#include <upcxx/upcxx.hpp>

/*
 * An "unrolled linked list" data structure that stores multiple
 * elements in a node, with custom serialization.
 */

static constexpr std::size_t NODE_CAPACITY = 16;

class UnrolledList {
  struct Node {
    int data[NODE_CAPACITY];
    int node_size;
    Node* next;
  };

  Node* first;
  std::size_t size_;

  Node* extend(); // allocate a new Node and place at the end

public:
  UnrolledList() : first(nullptr), size_(0) {}

  void push_back(int datum);

  struct upcxx_serialization {
    template<typename Writer>
    static void serialize(Writer &writer, const UnrolledList &obj) {
      writer.write(obj.size_);
      for (Node* crnt = obj.first; crnt; crnt = crnt->next) {
        writer.write_sequence(crnt->data, crnt->data + crnt->node_size);
      }
    }
    template<typename Reader, typename Storage>
    static UnrolledList* deserialize(Reader &reader, Storage storage) {
      UnrolledList* result = storage.construct();
      std::size_t count = reader.template read<std::size_t>();
      result->size_ = count;
      for (std::size_t read_count = 0; read_count < count;
           read_count += NODE_CAPACITY) {
        Node* node = result->extend();
        node->node_size = std::min(count - read_count, NODE_CAPACITY);
        reader.template read_sequence_overwrite<int>(
          node->data,
          node->node_size
        );
      }
      return result;
    }
  };

private:
  Node* get_last();

public:
  template<typename value_type>
  struct iterator_impl {
    Node* node;
    int index;
    explicit iterator_impl(Node* node = nullptr) : node(node), index(0) {}
    iterator_impl& operator++() {
      if (++index == node->node_size) {
        node = node->next;
        index = 0;
      }
      return *this;
    }
    iterator_impl operator++(int) {
      iterator_impl result = *this;
      ++*this;
      return result;
    }
    value_type& operator*() const {
      return node->data[index];
    }
    value_type* operator->() const {
      return node->data + index;
    }
    bool operator==(iterator_impl rhs) const {
      return node == rhs.node && index == rhs.index;
    }
    bool operator!=(iterator_impl rhs) const {
      return !(*this == rhs);
    }
  };

  using iterator = iterator_impl<int>;
  using const_iterator = iterator_impl<const int>;

  iterator begin() {
    return iterator(first);
  }
  iterator end() {
    return iterator();
  }
  const_iterator begin() const {
    return const_iterator(first);
  }
  const_iterator end() const {
    return const_iterator();
  }
  const_iterator cbegin() const {
    return const_iterator(first);
  }
  const_iterator cend() const {
    return const_iterator();
  }

  template<typename Container>
  bool operator==(const Container &rhs) const {
    auto b1 = begin(), e1 = end();
    auto b2 = std::begin(rhs), e2 = std::end(rhs);
    for (; b1 != e1 && b2 != e2; ++b1, ++b2) {
      if (*b1 != *b2) {
        return false;
      }
    }
    return b1 == e1 && b2 == e2;
  }

  UnrolledList(const UnrolledList &rhs) : UnrolledList() {
    for (auto it = rhs.begin(); it != rhs.end(); ++it) {
      push_back(*it);
    }
  }
  ~UnrolledList() {
    while (first) {
      Node *next = first->next;
      delete first;
      first = next;
    }
  }
  UnrolledList& operator=(const UnrolledList &rhs) {
    if (&rhs != this) {
      UnrolledList copy = rhs;
      std::swap(first, copy.first);
      std::swap(size_, copy.size_);
    }
    return *this;
  }
};

UnrolledList::Node* UnrolledList::get_last() {
  if (!first) return nullptr;
  Node* crnt = first;
  for (; crnt->next; crnt = crnt->next);
  return crnt;
}

UnrolledList::Node* UnrolledList::extend() {
  Node* node = new Node{{}, 0, nullptr};
  Node* last = get_last();
  (last ? last->next : first) = node;
  return node;
}

void UnrolledList::push_back(int datum) {
  Node* last = get_last();
  if (!last || last->node_size == NODE_CAPACITY) {
    last = extend();
  }
  last->data[last->node_size++] = datum;
  ++size_;
}

std::ostream& operator<<(std::ostream &os, const UnrolledList &ul) {
  for (auto it = ul.begin(); it != ul.end(); ++it) {
    os << *it << " ";
  }
  return os;
}

template<typename Container>
void fill(Container &container, int start, int end) {
  for (int i = start; i < end; ++i) {
    container.push_back(i);
  }
}

int main() {
  upcxx::init();
  int rank = upcxx::rank_me();
  int right = (upcxx::rank_me() + 1) % upcxx::rank_n();

  UnrolledList u1;
  fill(u1, rank, rank + 40);
  upcxx::experimental::say() << u1;
  std::vector<int> v1;
  fill(v1, rank, rank + 40);
  UPCXX_ASSERT_ALWAYS(u1 == v1);

  for (int i = 0; i < 10 * static_cast<int>(NODE_CAPACITY); ++i) {
    UnrolledList u2;
    fill(u2, rank, rank + i);
    std::vector<int> v2;
    fill(v2, rank, rank + i);
    UPCXX_ASSERT_ALWAYS(u2 == v2);

    UnrolledList u3 =
      upcxx::serialization_traits<UnrolledList>::deserialized_value(u2);
    UPCXX_ASSERT_ALWAYS(u3 == u2);
    UPCXX_ASSERT_ALWAYS(u3 == v2);

    UnrolledList u4;
    fill(u4, right, right + i);
    upcxx::rpc(right, [](const UnrolledList &received, int j) {
                        std::vector<int> expected;
                        fill(expected, upcxx::rank_me(),
                             upcxx::rank_me() + j);
                        UPCXX_ASSERT_ALWAYS(received == expected);
                      }, u4, i).wait();
  }

  upcxx::finalize();
  if (rank == 0) {
    std::cout << "SUCCESS" << std::endl;
  }
}
