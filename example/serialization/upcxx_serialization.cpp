#include <upcxx/upcxx.hpp>
#include <iostream>
#include <map>
#include <vector>

/*
 * A distributed graph data structure that stores an extended neighborhood for
 * each vertex (radius=2) with the ability to dynamically create edge.
 *
 * Vertex datastructures contain an ID that is unique in the job, a pointer to
 * the rank that owns it, and a list of vertex* pointers that point to vertices
 * it has edges with.
 *
 * When a new edge is created between two vertices, we send an RPC to each
 * endpoint of the edge which captures the other endpoint including its neighbor
 * list. On the owning rank, we update the target vertex with this new edge as
 * well as any second-degree edges this creates.
 *
 * While std::vector is serializable and vertex* is TriviallySerializable, this
 * example requires custom serialization because a vertex* is not usable on any
 * node except the one that created it.
 */

int total_num_vertices;
int rank;
int nranks;

#define VERTICES_PER_RANK ((total_num_vertices + nranks - 1) / nranks)
#define OWNER_RANK(vertex_id) ((vertex_id) / VERTICES_PER_RANK)
#define START_RANK_VERTICES(_rank) ((_rank) * VERTICES_PER_RANK)
#define END_RANK_VERTICES(_rank) (((_rank) + 1) * VERTICES_PER_RANK > total_num_vertices ? total_num_vertices : ((_rank) + 1) * VERTICES_PER_RANK)

class vertex;
vertex *get_vertex_from_store(int vertex_id);

class vertex {
    private:
        int id;
        std::vector<vertex *> neighbors;
        std::vector<vertex *> neighbors_of_neighbors;

    public:
        vertex(int _id) : id(_id) { }

        int get_id() const { return id; }

        bool has_edge(int other) const {
            for (auto i = neighbors.begin(), e = neighbors.end(); i != e; i++) {
                vertex *v = *i;
                if (v->get_id() == other) return true;
            }
            return false;
        }

        bool has_neighbor_of_neighbor(int other) const {
            for (auto i = neighbors_of_neighbors.begin(),
                    e = neighbors_of_neighbors.end(); i != e; i++) {
                vertex *v = *i;
                if (v->get_id() == other) return true;
            }
            return false;
        }

        void add_neighbor(int neighbor_id) {
            vertex *neighbor = get_vertex_from_store(neighbor_id);
            if (std::find(neighbors.begin(), neighbors.end(), neighbor) ==
                    neighbors.end()) {
                neighbors.push_back(neighbor);
            }
        }

        void add_neighbor_of_neighbor(int neighbor_of_neighbor_id) {
            vertex *neighbor_of_neighbor = get_vertex_from_store(
                    neighbor_of_neighbor_id);
            if (std::find(neighbors_of_neighbors.begin(),
                        neighbors_of_neighbors.end(), neighbor_of_neighbor) ==
                    neighbors_of_neighbors.end()) {
                neighbors_of_neighbors.push_back(neighbor_of_neighbor);
            }
        }

        std::vector<vertex*>::const_iterator neighbors_begin() const {
            return neighbors.begin();
        }

        std::vector<vertex*>::const_iterator neighbors_end() const {
            return neighbors.end();
        }

        std::vector<vertex*>::const_iterator neighbors_of_neighbors_begin() const {
            return neighbors_of_neighbors.begin();
        }

        std::vector<vertex*>::const_iterator neighbors_of_neighbors_end() const {
            return neighbors_of_neighbors.end();
        }

        int n_neighbors() const { return neighbors.size(); }

        // Serialize a vertex and its neighbor list in to a byte stream
        template<typename Writer>
        static void serialize_vertex(Writer& writer, vertex const & object) {
            writer.write(object.get_id());
            writer.write(object.n_neighbors());
            for (auto i = object.neighbors_begin(), e = object.neighbors_end();
                    i != e; i++) {
                vertex *neighbor = *i;
                writer.write(neighbor->get_id());
            }
        }

        // Deserialize a vertex and its neighbor list from a byte stream
        template<typename Reader, typename Storage>
        static vertex* deserialize_vertex(Reader& reader, Storage storage) {
            int id = reader.template read<int>();
            int n_neighbors = reader.template read<int>();

            vertex *v = storage.construct(id);
            for (int n = 0; n < n_neighbors; n++) {
                v->add_neighbor(reader.template read<int>());
            }
            return v;
        }

#ifdef NESTED_SERIALIZATION
        /*
         * An example of using a member struct upcxx_serialization to implement
         * custom serialization for the vertex class.
         */
        struct upcxx_serialization {
            template<typename Writer>
            static void serialize (Writer& writer, vertex const & object) {
                serialize_vertex(writer, object);
            }

            template<typename Reader, typename Storage>
            static vertex* deserialize(Reader& reader, Storage storage) {
                return deserialize_vertex(reader, storage);
            }
        };
#endif
};

#ifndef NESTED_SERIALIZATION
/*
 * An example specialization of upcxx::serialization for the vertex class.
 */
namespace upcxx {
template<>
struct serialization<vertex> {
    public:
        template<typename Writer>
        static void serialize (Writer& writer, vertex const & object) {
            vertex::serialize_vertex(writer, object);
        }

        template<typename Reader, typename Storage>
        static vertex* deserialize(Reader& reader, Storage storage) {
            return vertex::deserialize_vertex(reader, storage);
        }
};
}
#endif

/*
 * A mapping from vertex ID to its local data structure. Vertices in this data
 * structure may be owned by this rank or a remote rank. Locally owned vertices
 * will store their neighbors and neighbors_of_neighbors list. Remotely owned
 * vertices are only maintained to be pointed at from the neighbor lists in
 * locally owned vertices.
 */
std::map<int, vertex *> local_vertex_store;

/*
 * Fetch a vertex* from the local_vertex_store for a given vertex ID, inserting
 * a new entry as needed.
 */
vertex *get_vertex_from_store(int vertex_id) {
    if (local_vertex_store.find(vertex_id) == local_vertex_store.end()) {
        UPCXX_ASSERT(OWNER_RANK(vertex_id) != rank);
        vertex *new_vert = new vertex(vertex_id);
        local_vertex_store.insert(std::pair<int, vertex*>(vertex_id, new_vert));
    }

    auto iter = local_vertex_store.find(vertex_id);
    UPCXX_ASSERT(iter != local_vertex_store.end());
    return iter->second;
}

void create_edge(vertex* local, int other_id) {
    int local_id = local->get_id();
    get_vertex_from_store(other_id); // ensure existence

    /*
     * Add a vertex* for this ID to our neighbors list. This should
     * either be an existing vertex* pulled from local_vertex_store or a
     * newly created vertex*, which should be inserted in
     * local_vertex_store.
     */
    local->add_neighbor(other_id);

    /*
     * For each of my neighbors, inform them they have a new
     * neighbor-of-neighbor.
     */
    upcxx::future<> neighbors_fut = upcxx::make_future();
    for (auto i = local->neighbors_begin(), e = local->neighbors_end(); i != e;
            i++) {
        vertex *neighbor = *i;
        neighbors_fut = upcxx::when_all(neighbors_fut,
                upcxx::rpc(OWNER_RANK(neighbor->get_id()),
                    [other_id] (int id) {
                        vertex *remote_neighbor = get_vertex_from_store(id);
                        remote_neighbor->add_neighbor_of_neighbor(other_id);
                    }, neighbor->get_id()));
    }
    neighbors_fut.wait();

    /*
     * Ship this vertex and its neighbors to the rank that owns
     * other_id. Use an RPC to then add this vertex to its neighbors,
     * and add this vertex's neighbors to its neighbors of neighbors.
     * Fetch that vertex back.
     */
    upcxx::future<vertex> other_fut = upcxx::rpc(OWNER_RANK(other_id),
            [] (const vertex &tmp, int other_id) {
                vertex *other = get_vertex_from_store(other_id);

                other->add_neighbor(tmp.get_id());

                for (auto i = tmp.neighbors_begin(), e = tmp.neighbors_end();
                        i != e; i++) {
                    vertex *n = *i;
                    other->add_neighbor_of_neighbor(n->get_id());
                }

                return *other;
        }, *local, other_id);

    /*
     * With the returned vertex, update my neighbors_of_neighbors with
     * its neighbors and inform them I am their neighbor of neighbor as well.
     */
    vertex transferred_other = other_fut.wait();

    upcxx::future<> fut = upcxx::make_future();
    for (auto i = transferred_other.neighbors_begin(),
            e = transferred_other.neighbors_end(); i != e; i++) {
        int id = (*i)->get_id();
        local->add_neighbor_of_neighbor(id);

        fut = upcxx::when_all(fut, upcxx::rpc(OWNER_RANK(id), [id, local_id] {
                    vertex *remote = get_vertex_from_store(id);
                    remote->add_neighbor_of_neighbor(local_id);
                }));
    }
    fut.wait();
}

int main(void) {
    upcxx::init();
    rank = upcxx::rank_me();
    nranks = upcxx::rank_n();

    srand(rank);

    int niters = 10;
    int edges_per_iter = 10;
    total_num_vertices = nranks * niters * edges_per_iter;

    int start_local_vertices = START_RANK_VERTICES(rank);
    int end_local_vertices = END_RANK_VERTICES(rank);

    int *edges_to_insert = new int[niters * edges_per_iter * 2];
    UPCXX_ASSERT(edges_to_insert);
    for (int i = 0; i < niters * edges_per_iter; i++) {
        // Choose a random local vertex
        edges_to_insert[2 * i] = start_local_vertices + (rand() %
                (end_local_vertices - start_local_vertices));
        // Choose a random vertex
        edges_to_insert[2 * i + 1] = (rand() % total_num_vertices);
    }

    upcxx::barrier();

    // Seed our local vertex store
    for (int v = start_local_vertices; v < end_local_vertices; v++) {
        vertex *new_vert = new vertex(v);
        local_vertex_store.insert(std::pair<int, vertex*>(v, new_vert));
    }

    upcxx::barrier();

    for (int iter = 0; iter < niters; iter++) {
        for (int e = iter * edges_per_iter; e < (iter + 1) * edges_per_iter;
                e++) {
            int a = edges_to_insert[2 * e];
            int b = edges_to_insert[2 * e + 1];
            vertex *v_a = get_vertex_from_store(a);
            create_edge(v_a, b);
        }

        upcxx::barrier();
    }

    upcxx::barrier();

    /*
     * Validation. Assert that each of the edges we were supposed to create were
     * created by checking local and remote state.
     */
    upcxx::future<> fut = upcxx::make_future();
    for (int e = 0; e < niters * edges_per_iter; e++) {
        int a = edges_to_insert[2 * e];
        int b = edges_to_insert[2 * e + 1];
        vertex *v_a = get_vertex_from_store(a);
        UPCXX_ASSERT(v_a->has_edge(b));

        fut = upcxx::when_all(fut, upcxx::rpc(OWNER_RANK(b), [a, b] {
                    vertex *vb = get_vertex_from_store(b);
                    UPCXX_ASSERT(vb->has_edge(a));
                }));
    }
    fut.wait();
    delete [] edges_to_insert;

    /*
     * Validation. Assert that for each of our local vertices, any vertex that
     * they consider to be a neighbor-of-a-neighbor also considers them to be a
     * neighbor-of-neighbor.
     */
    fut = upcxx::make_future();
    for (int v = start_local_vertices; v < end_local_vertices; v++) {
        vertex *vert = get_vertex_from_store(v);
        for (auto i = vert->neighbors_of_neighbors_begin(),
                e = vert->neighbors_of_neighbors_end(); i != e; i++) {
            vertex* n_of_n = *i;
            int n_of_n_id = n_of_n->get_id();
            fut = upcxx::when_all(fut, upcxx::rpc(OWNER_RANK(n_of_n->get_id()),
                        [v, n_of_n_id] {
                        vertex *n_of_n = get_vertex_from_store(n_of_n_id);
                        UPCXX_ASSERT(n_of_n->has_neighbor_of_neighbor(v));
                    }));

        }
    }
    fut.wait();
    upcxx::finalize();

    if (rank == 0) {
        printf("SUCCESS\n");
    }

    return 0;
}
