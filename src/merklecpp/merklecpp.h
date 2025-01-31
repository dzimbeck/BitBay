// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <sstream>
#include <stack>
#include <vector>

#ifdef MERKLECPP_TRACE_ENABLED
// Hashes in the trace output are truncated to TRACE_HASH_SIZE bytes.
#  define TRACE_HASH_SIZE 3

#  ifndef MERKLECPP_TRACE
#    include <iostream>
#    define MERKLECPP_TOUT std::cout
#    define MERKLECPP_TRACE(X) \
      { \
        X; \
        MERKLECPP_TOUT.flush(); \
      };
#  endif
#else
#  define MERKLECPP_TRACE(X)
#endif

#define MERKLECPP_VERSION_MAJOR 1
#define MERKLECPP_VERSION_MINOR 0
#define MERKLECPP_VERSION_PATCH 0

namespace merkle
{
  static inline uint32_t convert_endianness(uint32_t n)
  {
#if defined(htobe32)
    // If htobe32 happens to be a macro, use it.
    return htobe32(n);
#elif defined(__LITTLE_ENDIAN__) || defined(__LITTLE_ENDIAN)
    const uint32_t sz = sizeof(uint32_t);
    // Just as fast.
    uint32_t r = 0;
    for (size_t i = 0; i < sz; i++)
      r |= ((n >> (8 * ((sz - 1) - i))) & 0xFF) << (8 * i);
    return *reinterpret_cast<uint32_t*>(&r);
#else
    const uint32_t sz = sizeof(uint32_t);
    // A little slower, but works for both endiannesses.
    uint8_t r[8];
    for (size_t i = 0; i < sz; i++)
      r[i] = (n >> (8 * ((sz - 1) - i))) & 0xFF;
    return *reinterpret_cast<uint32_t*>(&r);
#endif
  }

  static inline void serialise_uint64_t(uint64_t n, std::vector<uint8_t>& bytes)
  {
    size_t sz = sizeof(uint64_t);
    bytes.reserve(bytes.size() + sz);
    for (uint64_t i = 0; i < sz; i++)
      bytes.push_back((n >> (8 * (sz - i - 1))) & 0xFF);
  }

  static inline uint64_t deserialise_uint64_t(
    const std::vector<uint8_t>& bytes, size_t& index)
  {
    uint64_t r = 0;
    uint64_t sz = sizeof(uint64_t);
    for (uint64_t i = 0; i < sz; i++)
      r |= static_cast<uint64_t>(bytes.at(index++)) << (8 * (sz - i - 1));
    return r;
  }

  /// @brief Template for fixed-size hashes
  /// @tparam SIZE Size of the hash in number of bytes
  template <size_t SIZE>
  struct HashT
  {
    /// Holds the hash bytes
    uint8_t bytes[SIZE];

    /// @brief Constructs a Hash with all bytes set to zero
    HashT<SIZE>()
    {
      std::fill(bytes, bytes + SIZE, 0);
    }

    /// @brief Constructs a Hash from a byte buffer
    /// @param bytes Buffer with hash value
    HashT<SIZE>(const uint8_t* bytes)
    {
      std::copy(bytes, bytes + SIZE, this->bytes);
    }

    /// @brief Constructs a Hash from a string
    /// @param s String to read the hash value from
    HashT<SIZE>(const std::string& s)
    {
      if (s.length() != 2 * SIZE)
        throw std::runtime_error("invalid hash string");
      for (size_t i = 0; i < SIZE; i++)
      {
        int tmp;
        sscanf(s.c_str() + 2 * i, "%02x", &tmp);
        bytes[i] = tmp;
      }
    }

    /// @brief Deserialises a Hash from a vector of bytes
    /// @param bytes Vector to read the hash value from
    HashT<SIZE>(const std::vector<uint8_t>& bytes)
    {
      if (bytes.size() < SIZE)
        throw std::runtime_error("not enough bytes");
      deserialise(bytes);
    }

    /// @brief Deserialises a Hash from a vector of bytes
    /// @param bytes Vector to read the hash value from
    /// @param position Position of the first byte in @p bytes
    HashT<SIZE>(const std::vector<uint8_t>& bytes, size_t& position)
    {
      if (bytes.size() - position < SIZE)
        throw std::runtime_error("not enough bytes");
      deserialise(bytes, position);
    }

    /// @brief Deserialises a Hash from an array of bytes
    /// @param bytes Array to read the hash value from
    HashT<SIZE>(const std::array<uint8_t, SIZE>& bytes)
    {
      std::copy(bytes.data(), bytes.data() + SIZE, this->bytes);
    }

    /// @brief The size of the hash (in number of bytes)
    size_t size() const
    {
      return SIZE;
    }

    /// @brief zeros out all bytes in the hash
    void zero()
    {
      std::fill(bytes, bytes + SIZE, 0);
    }

    /// @brief The size of the serialisation of the hash (in number of bytes)
    size_t serialised_size() const
    {
      return SIZE;
    }

    /// @brief Convert a hash to a hex-encoded string
    /// @param num_bytes The maximum number of bytes to convert
    /// @param lower_case Enables lower-case hex characters
    std::string to_string(size_t num_bytes = SIZE, bool lower_case = true) const
    {
      size_t num_chars = 2 * num_bytes;
      std::string r(num_chars, '_');
      for (size_t i = 0; i < num_bytes; i++)
        snprintf(
          const_cast<char*>(r.data() + 2 * i),
          num_chars + 1 - 2 * i,
          lower_case ? "%02x" : "%02X",
          bytes[i]);
      return r;
    }

    /// @brief Hash assignment operator
    HashT<SIZE> operator=(const HashT<SIZE>& other)
    {
      std::copy(other.bytes, other.bytes + SIZE, bytes);
      return *this;
    }

    /// @brief Hash equality operator
    bool operator==(const HashT<SIZE>& other) const
    {
      return memcmp(bytes, other.bytes, SIZE) == 0;
    }

    /// @brief Hash inequality operator
    bool operator!=(const HashT<SIZE>& other) const
    {
      return memcmp(bytes, other.bytes, SIZE) != 0;
    }

    /// @brief Serialises a hash
    /// @param buffer Buffer to serialise to
    void serialise(std::vector<uint8_t>& buffer) const
    {
      MERKLECPP_TRACE(MERKLECPP_TOUT << "> HashT::serialise " << std::endl);
      for (auto& b : bytes)
        buffer.push_back(b);
    }

    /// @brief Deserialises a hash
    /// @param buffer Buffer to read the hash from
    /// @param position Position of the first byte in @p bytes
    void deserialise(const std::vector<uint8_t>& buffer, size_t& position)
    {
      MERKLECPP_TRACE(MERKLECPP_TOUT << "> HashT::deserialise " << std::endl);
      if (buffer.size() - position < SIZE)
        throw std::runtime_error("not enough bytes");
      for (size_t i = 0; i < sizeof(bytes); i++)
        bytes[i] = buffer[position++];
    }

    /// @brief Deserialises a hash
    /// @param buffer Buffer to read the hash from
    void deserialise(const std::vector<uint8_t>& buffer)
    {
      size_t position = 0;
      deserialise(buffer, position);
    }

    /// @brief Conversion operator to vector of bytes
    operator std::vector<uint8_t>() const
    {
      std::vector<uint8_t> bytes;
      serialise(bytes);
      return bytes;
    }
  };

  /// @brief Template for Merkle paths
  /// @tparam HASH_SIZE Size of each hash in number of bytes
  /// @tparam HASH_FUNCTION The hash function
  template <
    size_t HASH_SIZE,
    void HASH_FUNCTION(
      const HashT<HASH_SIZE>& l,
      const HashT<HASH_SIZE>& r,
      HashT<HASH_SIZE>& out,
      bool & swap)>
  class PathT
  {
  public:
    /// @brief Path direction
    typedef enum
    {
      PATH_LEFT,
      PATH_RIGHT
    } Direction;

    /// @brief Path element
    typedef struct
    {
      /// @brief The hash of the path element
      HashT<HASH_SIZE> hash;

      /// @brief The direction at which @p hash joins at this path element
      /// @note If @p direction == PATH_LEFT, @p hash joins at the left, i.e.
      /// if t is the current hash, e.g. a leaf, then t' = Hash( @p hash, t );
      Direction direction;
    } Element;

    /// @brief Path constructor
    /// @param leaf
    /// @param leaf_index
    /// @param elements
    /// @param max_index
    PathT(
      const HashT<HASH_SIZE>& leaf,
      size_t leaf_index,
      std::list<Element>&& elements,
      size_t max_index) :
      _leaf(leaf),
      _leaf_index(leaf_index),
      _max_index(max_index),
      elements(elements)
    {}

    /// @brief Path copy constructor
    /// @param other Path to copy
    PathT(const PathT& other)
    {
      _leaf = other._leaf;
      elements = other.elements;
    }

    /// @brief Path move constructor
    /// @param other Path to move
    PathT(PathT&& other)
    {
      _leaf = std::move(other._leaf);
      elements = std::move(other.elements);
    }

    /// @brief The number of elements on the path
    size_t size() const
    {
      return elements.size();
    }

    /// @brief The size of the serialised path in number of bytes
    size_t serialised_size() const
    {
      return sizeof(_leaf) +
      sizeof(uint64_t) + // leaf index
      sizeof(uint64_t) + // max index
      sizeof(uint64_t) + // number of elements
      elements.size() * (
        sizeof(Element::hash) + // hash
        sizeof(uint8_t) // direction
      );
    }

    /// @brief Index of the leaf of the path
    size_t leaf_index() const
    {
      return _leaf_index;
    }

    /// @brief Maximum index of the tree at the time the path was extracted
    size_t max_index() const
    {
      return _max_index;
    }

    /// @brief Operator to extract the hash of a given path element
    /// @param i Index of the path element
    const HashT<HASH_SIZE>& operator[](size_t i) const
    {
      return std::next(begin(), i)->hash;
    }

    /// @brief Iterator for path elements
    typedef typename std::list<Element>::const_iterator const_iterator;

    /// @brief Start iterator for path elements
    const_iterator begin() const
    {
      return elements.begin();
    }

    /// @brief End iterator for path elements
    const_iterator end() const
    {
      return elements.end();
    }

    /// @brief Convert a path to a string
    /// @param num_bytes The maximum number of bytes to convert
    /// @param lower_case Enables lower-case hex characters
    std::string to_string(
      size_t num_bytes = HASH_SIZE, bool lower_case = true) const
    {
      std::stringstream stream;
      stream << _leaf.to_string(num_bytes);
      for (auto& e : elements)
        stream << " " << e.hash.to_string(num_bytes, lower_case)
               << (e.direction == PATH_LEFT ? "(L)" : "(R)");
      return stream.str();
    }

    /// @brief The leaf hash of the path
    const HashT<HASH_SIZE>& leaf() const
    {
      return _leaf;
    }

    /// @brief Equality operator for paths
    bool operator==(const PathT<HASH_SIZE, HASH_FUNCTION>& other) const
    {
      if (_leaf != other._leaf || elements.size() != other.elements.size())
        return false;
      auto it = elements.begin();
      auto other_it = other.elements.begin();
      while (it != elements.end() && other_it != other.elements.end())
      {
        if (it->hash != other_it->hash || it->direction != other_it->direction)
          return false;
        it++;
        other_it++;
      }
      return true;
    }

    /// @brief Inequality operator for paths
    bool operator!=(const PathT<HASH_SIZE, HASH_FUNCTION>& other)
    {
      return !this->operator==(other);
    }

  protected:
    /// @brief The leaf hash
    HashT<HASH_SIZE> _leaf;

    /// @brief The index of the leaf
    size_t _leaf_index;

    /// @brief The maximum leaf index of the tree at the time of path extraction
    size_t _max_index;

    /// @brief The elements of the path
    std::list<Element> elements;
  };

  /// @brief Template for Merkle trees
  /// @tparam HASH_SIZE Size of each hash in number of bytes
  /// @tparam HASH_FUNCTION The hash function
  template <
    size_t HASH_SIZE,
    void HASH_FUNCTION(
      const HashT<HASH_SIZE>& l,
      const HashT<HASH_SIZE>& r,
      HashT<HASH_SIZE>& out,
      bool & swap)>
  class TreeT
  {
  protected:
    /// @brief The structure of tree nodes
    struct Node
    {
      /// @brief Constructs a new tree node
      /// @param hash The hash of the node
      static Node* make(const HashT<HASH_SIZE>& hash)
      {
        auto r = new Node();
        r->swap = false;
        r->left = r->right = nullptr;
        r->hash = hash;
        r->dirty = false;
        r->update_sizes();
        assert(r->invariant());
        return r;
      }

      /// @brief Constructs a new tree node
      /// @param left The left child of the new node
      /// @param right The right child of the new node
      static Node* make(Node* left, Node* right)
      {
        assert(left && right);
        auto r = new Node();
        r->swap = false;
        r->left = left;
        r->right = right;
        r->dirty = true;
        r->update_sizes();
        assert(r->invariant());
        return r;
      }

      /// @brief Checks invariant of a tree node
      /// @note This indicates whether some basic properties of the tree
      /// construction are violated.
      bool invariant()
      {
        bool c1 = (left && right) || (!left && !right);
        bool c2 = !left || !right || (size == left->size + right->size + 1);
        bool cl = !left || left->invariant();
        bool cr = !right || right->invariant();
        bool ch = height <= sizeof(size) * 8;
        bool r = c1 && c2 && cl && cr && ch;
        return r;
      }

      ~Node()
      {
        assert(invariant());
        // Potential future improvement: remove recursion and keep nodes for
        // future insertions
        delete (left);
        delete (right);
      }

      /// @brief Indicates whether a subtree is full
      /// @note A subtree is full if the number of nodes under a tree is
      /// 2**height-1.
      bool is_full() const
      {
        size_t max_size = (1 << height) - 1;
        assert(size <= max_size);
        return size == max_size;
      }

      /// @brief Updates the tree size and height of the subtree under a node
      void update_sizes()
      {
        if (left && right)
        {
          size = left->size + right->size + 1;
          height = std::max(left->height, right->height) + 1;
        }
        else
          size = height = 1;
      }

      /// @brief The Hash of the node
      HashT<HASH_SIZE> hash;

      /// @brief The left and right to be swapped 
      bool swap;

      /// @brief The left child of the node
      Node* left;

      /// @brief The right child of the node
      Node* right;

      /// @brief The size of the subtree
      size_t size;

      /// @brief The height of the subtree
      uint8_t height;

      /// @brief Dirty flag for the hash
      /// @note The @p hash is only correct if this flag is false, otherwise
      /// it needs to be computed by calling hash() on the node.
      bool dirty;
    };

  public:
    /// @brief The type of hashes in the tree
    typedef HashT<HASH_SIZE> Hash;

    /// @brief The type of paths in the tree
    typedef PathT<HASH_SIZE, HASH_FUNCTION> Path;

    /// @brief The type of the tree
    typedef TreeT<HASH_SIZE, HASH_FUNCTION> Tree;

    /// @brief Constructs an empty tree
    TreeT() {}

    /// @brief Copies a tree
    TreeT(const TreeT& other)
    {
      *this = other;
    }

    /// @brief Moves a tree
    /// @param other Tree to move
    TreeT(TreeT&& other) :
      leaf_nodes(std::move(other.leaf_nodes)),
      uninserted_leaf_nodes(std::move(other.uninserted_leaf_nodes)),
      _root(std::move(other._root)),
      num_flushed(other.num_flushed),
      insertion_stack(std::move(other.insertion_stack)),
      hashing_stack(std::move(other.hashing_stack)),
      walk_stack(std::move(other.walk_stack))
    {}

    /// @brief Constructs a tree containing one root hash
    /// @param root Root hash of the tree
    TreeT(const Hash& root)
    {
      insert(root);
    }

    /// @brief Deconstructor
    ~TreeT()
    {
      delete (_root);
      for (auto n : uninserted_leaf_nodes)
        delete (n);
    }

    /// @brief Invariant of the tree
    bool invariant()
    {
      return _root ? _root->invariant() : true;
    }

    /// @brief Inserts a hash into the tree
    /// @param hash Hash to insert
    void insert(const uint8_t* hash)
    {
      insert(Hash(hash));
    }

    /// @brief Inserts a hash into the tree
    /// @param hash Hash to insert
    void insert(const Hash& hash)
    {
      MERKLECPP_TRACE(MERKLECPP_TOUT << "> insert "
                                     << hash.to_string(TRACE_HASH_SIZE)
                                     << std::endl;);
      uninserted_leaf_nodes.push_back(Node::make(hash));
      statistics.num_insert++;
    }

    /// @brief Inserts multiple hashes into the tree
    /// @param hashes Vector of hashes to insert
    void insert(const std::vector<Hash>& hashes)
    {
      for (auto hash : hashes)
        insert(hash);
    }

    /// @brief Inserts multiple hashes into the tree
    /// @param hashes List of hashes to insert
    void insert(const std::list<Hash>& hashes)
    {
      for (auto hash : hashes)
        insert(hash);
    }

    /// @brief Extracts the root hash of the tree
    /// @return The root hash
    const Hash& root()
    {
      MERKLECPP_TRACE(MERKLECPP_TOUT << "> root" << std::endl;);
      statistics.num_root++;
      compute_root();
      assert(_root && !_root->dirty);
      MERKLECPP_TRACE(MERKLECPP_TOUT
                        << " - root: " << _root->hash.to_string(TRACE_HASH_SIZE)
                        << std::endl;);
      return _root->hash;
    }

    /// @brief Walks along the path from the root of a tree to a leaf
    /// @param index The leaf index to walk to
    /// @param update Flag to enable re-computation of node fields (like
    /// subtree size) while walking
    /// @param f Function to call for each node on the path; the Boolean
    /// indicates whether the current step is a right or left turn.
    /// @return The final leaf node in the walk
    inline Node* walk_to(
      size_t index, bool update, const std::function<bool(Node*&, bool, bool)>&& f)
    {
      if (index < min_index() || max_index() < index)
        throw std::runtime_error("invalid leaf index");

      compute_root();

      assert(index < _root->size);

      Node* cur = _root;
      size_t it = 0;
      if (_root->height > 1)
        it = index << (sizeof(index) * 8 - _root->height + 1);
      assert(walk_stack.empty());

      for (uint8_t height = _root->height; height > 1;)
      {
        assert(cur->invariant());
        bool go_right = (it >> (8 * sizeof(it) - 1)) & 0x01;
        if (update)
          walk_stack.push_back(cur);
        bool swap = cur->swap;
        MERKLECPP_TRACE(MERKLECPP_TOUT
                          << " - at " << cur->hash.to_string(TRACE_HASH_SIZE)
                          << " (" << cur->size << "/" << (unsigned)cur->height
                          << ")"
                          << " (" << (go_right ? "R" : "L") << " swp:" << swap <<")"
                          << std::endl;);
        if (cur->height == height)
        {
          if (!f(cur, go_right, swap))
            continue;
          cur = (go_right ? cur->right : cur->left);
        }
        it <<= 1;
        height--;
      }

      if (update)
        while (!walk_stack.empty())
        {
          walk_stack.back()->update_sizes();
          walk_stack.pop_back();
        }

      return cur;
    }

    /// @brief Extracts the path from a leaf index to the root of the tree
    /// @param index The leaf index of the path to extract
    /// @return The path
    std::shared_ptr<Path> path(size_t index)
    {
      MERKLECPP_TRACE(MERKLECPP_TOUT << "> path from " << index << std::endl;);
      statistics.num_paths++;
      std::list<typename Path::Element> elements;

      walk_to(index, false, [&elements](Node* n, bool go_right, bool swap) {
        typename Path::Element e;
        bool use_hash_right = go_right;
        e.hash = use_hash_right ? n->left->hash : n->right->hash;
        e.direction = go_right ? Path::PATH_LEFT : Path::PATH_RIGHT;
        elements.push_front(std::move(e));
        return true;
      });

      return std::make_shared<Path>(
        leaf_node(index)->hash, index, std::move(elements), max_index());
    }

    /// @brief Operator to extract a leaf hash from the tree
    /// @param index Leaf index of the leaf to extract
    /// @return The leaf hash
    const Hash& operator[](size_t index) const
    {
      return leaf(index);
    }

    /// @brief Extract a leaf hash from the tree
    /// @param index Leaf index of the leaf to extract
    /// @return The leaf hash
    const Hash& leaf(size_t index) const
    {
      MERKLECPP_TRACE(MERKLECPP_TOUT << "> leaf " << index << std::endl;);
      if (index >= num_leaves())
        throw std::runtime_error("leaf index out of bounds");
      if (index - num_flushed >= leaf_nodes.size())
        return uninserted_leaf_nodes
          .at(index - num_flushed - leaf_nodes.size())
          ->hash;
      else
        return leaf_nodes.at(index - num_flushed)->hash;
    }

    /// @brief Number of leaves in the tree
    /// @note This is the abstract number of leaves in the tree (including
    /// flushed leaves), not the number of nodes in memory.
    /// @return The number of leaves in the tree
    size_t num_leaves() const
    {
      return num_flushed + leaf_nodes.size() + uninserted_leaf_nodes.size();
    }

    /// @brief Minimum leaf index
    /// @note The smallest leaf index for which it is safe to extract roots and
    /// paths.
    /// @return The minumum leaf index
    size_t min_index() const
    {
      return num_flushed;
    }

    /// @brief Maximum leaf index
    /// @note The greatest leaf index for which it is safe to extract roots and
    /// paths.
    /// @return The maximum leaf index
    size_t max_index() const
    {
      auto n = num_leaves();
      return n == 0 ? 0 : n - 1;
    }

    /// @brief Indicates whether the tree is empty
    /// @return Boolean that indicates whether the tree is empty
    bool empty() const
    {
      return num_leaves() == 0;
    }

    /// @brief Computes the size of the tree
    /// @note This is the number of nodes in the tree, including leaves and
    /// internal nodes.
    /// @return The size of the tree
    size_t size()
    {
      if (!uninserted_leaf_nodes.empty())
        insert_leaves();
      return _root ? _root->size : 0;
    }

    /// @brief Structure to hold statistical information
    mutable struct Statistics
    {
      /// @brief The number of hashes taken by the tree via hash()
      size_t num_hash = 0;

      /// @brief The number of insert() opertations performed on the tree
      size_t num_insert = 0;

      /// @brief The number of root() opertations performed on the tree
      size_t num_root = 0;

      /// @brief The number of past_root() opertations performed on the tree
      size_t num_past_root = 0;

      /// @brief The number of flush_to() opertations performed on the tree
      size_t num_flush = 0;

      /// @brief The number of retract_to() opertations performed on the tree
      size_t num_retract = 0;

      /// @brief The number of paths extracted from the tree via path()
      size_t num_paths = 0;

      /// @brief The number of past paths extracted from the tree via
      /// past_path()
      size_t num_past_paths = 0;

      /// @brief String representation of the statistics
      std::string to_string() const
      {
        std::stringstream stream;
        stream << "num_insert=" << num_insert << " num_hash=" << num_hash
               << " num_root=" << num_root << " num_retract=" << num_retract
               << " num_flush=" << num_flush << " num_paths=" << num_paths
               << " num_past_paths=" << num_past_paths;
        return stream.str();
      }
    }
    /// @brief Statistics
    statistics;

    /// @brief Prints an ASCII representation of the tree to a stream
    /// @param num_bytes The number of bytes of each node hash to print
    /// @return A string representing the tree
    std::string to_string(size_t num_bytes = HASH_SIZE) const
    {
      static const std::string dirty_hash(2 * num_bytes, '?');
      std::stringstream stream;
      std::vector<Node*> level, next_level;

      if (num_leaves() == 0)
      {
        stream << "<EMPTY>" << std::endl;
        return stream.str();
      }

      if (!_root)
      {
        stream << "No root." << std::endl;
      }
      else
      {
        size_t level_no = 0;
        level.push_back(_root);
        while (!level.empty())
        {
          stream << level_no++ << ": ";
          for (auto n : level)
          {
            stream << (n->dirty ? dirty_hash : n->hash.to_string(num_bytes));
            stream << "(" << n->size << "," << (unsigned)n->height << ")";
            if (n->left)
              next_level.push_back(n->left);
            if (n->right)
              next_level.push_back(n->right);
            stream << " ";
          }
          stream << std::endl << std::flush;
          std::swap(level, next_level);
          next_level.clear();
        }
      }

      stream << "+: "
             << "leaves=" << leaf_nodes.size() << ", "
             << "uninserted leaves=" << uninserted_leaf_nodes.size() << ", "
             << "flushed=" << num_flushed << std::endl;
      stream << "S: " << statistics.to_string() << std::endl;

      return stream.str();
    }

  protected:
    /// @brief Vector of leaf nodes current in the tree
    std::vector<Node*> leaf_nodes;

    /// @brief Vector of leaf nodes to be inserted in the tree
    /// @note These nodes are conceptually inserted, but no Node objects have
    /// been inserted for them yet.
    std::vector<Node*> uninserted_leaf_nodes;

    /// @brief Number of flushed nodes
    size_t num_flushed = 0;

    /// @brief Current root node of the tree
    Node* _root = nullptr;

  private:
    /// @brief The structure of elements on the insertion stack
    typedef struct
    {
      /// @brief The tree node to insert
      Node* n;
      /// @brief Flag to indicate whether @p n should be inserted into the
      ///  left or the right subtree of the current position in the tree.
      bool left;
    } InsertionStackElement;

    /// @brief The insertion stack
    /// @note To avoid actual recursion, this holds the stack/continuation for
    /// tree node insertion.
    mutable std::vector<InsertionStackElement> insertion_stack;

    /// @brief The hashing stack
    /// @note To avoid actual recursion, this holds the stack/continuation for
    /// hashing (parts of the) nodes of a tree.
    mutable std::vector<Node*> hashing_stack;

    /// @brief The walk stack
    /// @note To avoid actual recursion, this holds the stack/continuation for
    /// walking down the tree from the root to a leaf.
    mutable std::vector<Node*> walk_stack;

  protected:
    /// @brief Finds the leaf node corresponding to @p index
    /// @param index The leaf node index
    const Node* leaf_node(size_t index) const
    {
      MERKLECPP_TRACE(MERKLECPP_TOUT << "> leaf_node " << index << std::endl;);
      if (index >= num_leaves())
        throw std::runtime_error("leaf index out of bounds");
      if (index - num_flushed >= leaf_nodes.size())
        return uninserted_leaf_nodes.at(
          index - num_flushed - leaf_nodes.size());
      else
        return leaf_nodes.at(index - num_flushed);
    }

    /// @brief Computes the hash of a tree node
    /// @param n The tree node
    /// @param indent Indentation of trace output
    /// @note This recurses down the child nodes to compute intermediate
    /// hashes, if required.
    void hash(Node* n, size_t indent = 2) const
    {
#ifndef MERKLECPP_WITH_TRACE
      (void)indent;
#endif

      assert(hashing_stack.empty());
      hashing_stack.reserve(n->height);
      hashing_stack.push_back(n);

      while (!hashing_stack.empty())
      {
        n = hashing_stack.back();
        assert((n->left && n->right) || (!n->left && !n->right));

        if (n->left && n->left->dirty)
          hashing_stack.push_back(n->left);
        else if (n->right && n->right->dirty)
          hashing_stack.push_back(n->right);
        else
        {
          assert(n->left && n->right);
          HASH_FUNCTION(n->left->hash, n->right->hash, n->hash, n->swap);
          statistics.num_hash++;
          MERKLECPP_TRACE(
            MERKLECPP_TOUT << std::string(indent, ' ') << "+ h("
                           << n->left->hash.to_string(TRACE_HASH_SIZE) << ", "
                           << n->right->hash.to_string(TRACE_HASH_SIZE)
                           << ") == " << n->hash.to_string(TRACE_HASH_SIZE)
                           << " (" << n->size << "/" << (unsigned)n->height
                           << ")" << std::endl);
          n->dirty = false;
          hashing_stack.pop_back();
        }
      }
    }

    /// @brief Computes the root hash of the tree
    void compute_root()
    {
      insert_leaves(true);
      if (num_leaves() == 0)
        throw std::runtime_error("empty tree does not have a root");
      assert(_root);
      assert(_root->invariant());
      if (_root->dirty)
      {
        hash(_root);
        assert(_root && !_root->dirty);
      }
    }

    /// @brief Inserts one new leaf into the insertion stack
    /// @param n Current root node
    /// @param new_leaf New leaf node to insert
    /// @note This adds one new Node to the insertion stack/continuation for
    /// efficient processing by process_insertion_stack() later.
    void continue_insertion_stack(Node* n, Node* new_leaf)
    {
      while (true)
      {
        MERKLECPP_TRACE(MERKLECPP_TOUT << "  @ "
                                       << n->hash.to_string(TRACE_HASH_SIZE)
                                       << std::endl;);
        assert(n->invariant());

        if (n->is_full())
        {
          Node* result = Node::make(n, new_leaf);
          insertion_stack.push_back(InsertionStackElement());
          insertion_stack.back().n = result;
          return;
        }
        else
        {
          assert(n->left && n->right);
          insertion_stack.push_back(InsertionStackElement());
          InsertionStackElement& se = insertion_stack.back();
          se.n = n;
          n->dirty = true;
          if (!n->left->is_full())
          {
            se.left = true;
            n = n->left;
          }
          else
          {
            se.left = false;
            n = n->right;
          }
        }
      }
    }

    /// @brief Processes the insertion stack/continuation
    /// @param complete Indicates whether one element or the entire stack
    /// should be processed
    Node* process_insertion_stack(bool complete = true)
    {
      MERKLECPP_TRACE({
        std::string nodes;
        for (size_t i = 0; i < insertion_stack.size(); i++)
          nodes +=
            " " + insertion_stack.at(i).n->hash.to_string(TRACE_HASH_SIZE);
        MERKLECPP_TOUT << "  X " << (complete ? "complete" : "continue") << ":"
                       << nodes << std::endl;
      });

      Node* result = insertion_stack.back().n;
      insertion_stack.pop_back();

      assert(result->dirty);
      result->update_sizes();

      while (!insertion_stack.empty())
      {
        InsertionStackElement& top = insertion_stack.back();
        Node* n = top.n;
        bool left = top.left;
        insertion_stack.pop_back();

        if (left)
          n->left = result;
        else
          n->right = result;
        n->dirty = true;
        n->update_sizes();

        result = n;

        if (!complete && !result->is_full())
        {
          MERKLECPP_TRACE(MERKLECPP_TOUT
                            << "  X save "
                            << result->hash.to_string(TRACE_HASH_SIZE)
                            << std::endl;);
          return result;
        }
      }

      assert(result->invariant());

      return result;
    }

    /// @brief Inserts a new leaf into the tree
    /// @param root Current root node
    /// @param n New leaf node to insert
    void insert_leaf(Node*& root, Node* n)
    {
      MERKLECPP_TRACE(MERKLECPP_TOUT << " - insert_leaf "
                                     << n->hash.to_string(TRACE_HASH_SIZE)
                                     << std::endl;);
      leaf_nodes.push_back(n);
      if (insertion_stack.empty() && !root)
        root = n;
      else
      {
        continue_insertion_stack(root, n);
        root = process_insertion_stack(false);
      }
    }

    /// @brief Inserts multiple new leaves into the tree
    /// @param complete Indicates whether the insertion stack should be
    /// processed to completion after insertion
    void insert_leaves(bool complete = false)
    {
      if (!uninserted_leaf_nodes.empty())
      {
        MERKLECPP_TRACE(MERKLECPP_TOUT
                          << "* insert_leaves " << leaf_nodes.size() << " +"
                          << uninserted_leaf_nodes.size() << std::endl;);
        // Potential future improvement: make this go fast when there are many
        // leaves to insert.
        for (auto& n : uninserted_leaf_nodes)
          insert_leaf(_root, n);
        uninserted_leaf_nodes.clear();
      }
      if (complete && !insertion_stack.empty())
        _root = process_insertion_stack();
    }
  };

  /// @brief Type of hashes in the default tree type
  typedef HashT<32> Hash;

};
