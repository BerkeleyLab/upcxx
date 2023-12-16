#include <upcxx/upcxx.hpp>
#include <iostream>
#include "util.hpp"

#define N (4 * 1024 * 1024)

class val_chunk {
    public:
        int rank;
        int n;
        int *vals;

        val_chunk() { vals = nullptr; }

        ~val_chunk() { delete [] vals; vals = nullptr; }

        val_chunk(val_chunk && other) { 
          rank = other.rank;
          n = other.n;
          vals = other.vals;
          other.vals = nullptr; 
        }

        val_chunk(int _n) {
            n = _n;
            rank = upcxx::rank_me();
            vals = new int[n];
            assert(vals);
            for (int i = 0; i < n; i++) {
                vals[i] = rank + i;
            }
        }

        struct upcxx_serialization {
            template<typename Writer>
            static void serialize (Writer& writer, val_chunk const & object) {
                writer.write(object.rank);
                writer.write(object.n);
                int half = (int)(object.n / 2);
                writer.write_sequence(&object.vals[0], &object.vals[half],
                        half);
                for (int i = half; i < object.n; i++) {
                    writer.write(object.vals[i]);
                }
            }

            template<typename Reader, typename Storage>
            static val_chunk* deserialize(Reader& reader, Storage storage) {
                int rank = reader.template read<int>();
                int n = reader.template read<int>();

                val_chunk *v = storage.construct();
                v->rank = rank;
                v->n = n;
                v->vals = new int[n];

                int half = (int)(n / 2);
                reader.template read_sequence_into_iterator<int, int*>(v->vals, half);

                for (int i = half; i < n; i++) {
                    (v->vals)[i] = reader.template read<int>();
                }
                return v;
            }
        };
};

int main(void) {
    upcxx::init();
    print_test_header();

    int rank = upcxx::rank_me();
    int nranks = upcxx::rank_n();

    val_chunk chunk(N);

    upcxx::rpc((rank + 1) % nranks, [] (const val_chunk& chunk) {
                assert(chunk.n == N);
                for (int i = 0; i < N; i++) {
                    assert(chunk.vals[i] == chunk.rank + i);
                }
            }, std::move(chunk)).wait();

    print_test_success();
    upcxx::finalize();
    return 0;
}
