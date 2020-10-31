/*
  fastcluster: Fast hierarchical clustering routines for R and Python

  Copyright © 2011 Daniel Müllner
  <http://math.stanford.edu/~muellner>

  This library implements various fast algorithms for hierarchical,
  agglomerative clustering methods:

  (1) Algorithms for the "stored matrix approach": the input is the array of
      pairwise dissimilarities.

      MST_linkage_core: single linkage clustering with the "minimum spanning
      tree algorithm (Rohlfs)

      NN_chain_core: nearest-neighbor-chain algorithm, suitable for single,
      complete, average, weighted and Ward linkage (Murtagh)

      generic_linkage: generic algorithm, suitable for all distance update
      formulas (Müllner)

  (2) Algorithms for the "stored data approach": the input are points in a
      vector space.

      MST_linkage_core_vector: single linkage clustering for vector data

      generic_linkage_vector: generic algorithm for vector data, suitable for
      the Ward, centroid and median methods.

      generic_linkage_vector_alternative: alternative scheme for updating the
      nearest neighbors. This method seems faster than "generic_linkage_vector"
      for the centroid and median methods but slower for the Ward method.

  All these implementation treat infinity values correctly. They throw an
  exception if a NaN distance value occurs.
*/

#ifndef __GEODA_CENTER_FASTCLUSTER_H__
#define __GEODA_CENTER_FASTCLUSTER_H__

#include <iostream>
#include <cstddef> // for std::ptrdiff_t
#include <limits> // for std::numeric_limits<...>::infinity()
#include <algorithm> // for std::fill_n
#include <stdexcept> // for std::runtime_error
#include <string> // for std::string
#include <math.h>
#include <vector>
#include <stack>
#include "../ShapeOperations/GalWeight.h"

// Microsoft Visual Studio does not have fenv.h
#ifdef _MSC_VER
#if (_MSC_VER == 1500 || _MSC_VER == 1600)
#define NO_INCLUDE_FENV
#endif
#endif
#ifndef NO_INCLUDE_FENV
#include <fenv.h>
#endif

#include <cfloat> // also for DBL_MAX, DBL_MIN
#ifndef DBL_MANT_DIG
#error The constant DBL_MANT_DIG could not be defined.
#endif
#define T_FLOAT_MANT_DIG DBL_MANT_DIG

#ifndef LONG_MAX
#include <climits>
#endif
#ifndef LONG_MAX
#error The constant LONG_MAX could not be defined.
#endif
#ifndef INT_MAX
#error The constant INT_MAX could not be defined.
#endif

#ifndef INT32_MAX
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#endif

#ifndef HAVE_DIAGNOSTIC
#if __GNUC__ > 4 || (__GNUC__ == 4 && (__GNUC_MINOR__ >= 6))
#define HAVE_DIAGNOSTIC 1
#endif
#endif

typedef int_fast32_t t_index;
#ifndef INT32_MAX
#define MAX_INDEX 0x7fffffffL
#else
#define MAX_INDEX INT32_MAX
#endif
#if (LONG_MAX < MAX_INDEX)
#error The integer format "t_index" must not have a greater range than "long int".
#endif
#if (INT_MAX > MAX_INDEX)
#error The integer format "int" must not have a greater range than "t_index".
#endif
typedef double t_float;

#define fc_isnan(X) ((X)!=(X))

namespace fastcluster {
    
    enum method_codes {
        // non-Euclidean methods
        METHOD_METR_SINGLE           = 0,
        METHOD_METR_COMPLETE         = 1,
        METHOD_METR_AVERAGE          = 2,
        METHOD_METR_WEIGHTED         = 3,
        METHOD_METR_WARD             = 4,
        METHOD_METR_CENTROID         = 5,
        METHOD_METR_MEDIAN           = 6
    };
    
    enum {
        // Euclidean methods
        METHOD_VECTOR_SINGLE         = 0,
        METHOD_VECTOR_WARD           = 1,
        METHOD_VECTOR_CENTROID       = 2,
        METHOD_VECTOR_MEDIAN         = 3
    };
    
    enum {
        // Return values
        RET_SUCCESS        = 0,
        RET_MEMORY_ERROR   = 1,
        RET_STL_ERROR      = 2,
        RET_UNKNOWN_ERROR  = 3
    };
    
    // self-destructing array pointer
    template <typename type>
    class auto_array_ptr{
    private:
        type * ptr;
        auto_array_ptr(auto_array_ptr const &); // non construction-copyable
        auto_array_ptr& operator=(auto_array_ptr const &); // non copyable
    public:
        auto_array_ptr()
        : ptr(NULL)
        { }
        template <typename index>
        auto_array_ptr(index const size)
        : ptr(new type[size])
        { }
        template <typename index, typename value>
        auto_array_ptr(index const size, value const val)
        : ptr(new type[size])
        {
            std::fill_n(ptr, size, val);
        }
        ~auto_array_ptr() {
            delete [] ptr; }
        void free() {
            delete [] ptr;
            ptr = NULL;
        }
        template <typename index>
        void init(index const size) {
            ptr = new type [size];
        }
        template <typename index, typename value>
        void init(index const size, value const val) {
            init(size);
            std::fill_n(ptr, size, val);
        }
        inline operator type *() const { return ptr; }
    };
    
    struct node {
        t_index node1, node2;
        t_float dist;
        
        /*
         inline bool operator< (const node a) const {
         return this->dist < a.dist;
         }
         */
        
        inline friend bool operator< (const node a, const node b) {
            return (a.dist < b.dist);
        }
    };
    
    class cluster_result {
    private:
        auto_array_ptr<node> Z;
        t_index pos;
        
    public:
        cluster_result(const t_index size)
        : Z(size)
        , pos(0)
        {}
        
        void append(const t_index node1, const t_index node2, const t_float dist) {
            Z[pos].node1 = node1;
            Z[pos].node2 = node2;
            Z[pos].dist  = dist;
            ++pos;
        }
        
        node * operator[] (const t_index idx) const { return Z + idx; }
        
        /* Define several methods to postprocess the distances. All these functions
         are monotone, so they do not change the sorted order of distances. */
        
        void sqrt() const {
            for (node * ZZ=Z; ZZ!=Z+pos; ++ZZ) {
                ZZ->dist = ::sqrt(ZZ->dist);
            }
        }
        
        void sqrt(const t_float) const { // ignore the argument
            sqrt();
        }
        
        void sqrtdouble(const t_float) const { // ignore the argument
            for (node * ZZ=Z; ZZ!=Z+pos; ++ZZ) {
                ZZ->dist = ::sqrt(2*ZZ->dist);
            }
        }
        
        void power(const t_float p) const {
            t_float const q = 1/p;
            for (node * ZZ=Z; ZZ!=Z+pos; ++ZZ) {
                ZZ->dist = pow(ZZ->dist,q);
            }
        }
        
        void plusone(const t_float) const { // ignore the argument
            for (node * ZZ=Z; ZZ!=Z+pos; ++ZZ) {
                ZZ->dist += 1;
            }
        }
        
        void divide(const t_float denom) const {
            for (node * ZZ=Z; ZZ!=Z+pos; ++ZZ) {
                ZZ->dist /= denom;
            }
        }
    };
    
    // The size of a node is either 1 (a single point) or is looked up from
    // one of the clusters.
//#define size_(r_) ( ((r_<N) ? 1 : Z_(r_-N,3)) )
    
    /*
     Convenience class for the output array: automatic counter.
     */
    class linkage_output {
    private:
        t_float * Z;
        
    public:
        linkage_output(t_float * const Z_)
        : Z(Z_)
        {}
        
        void append(const t_index node1, const t_index node2, const t_float dist,
                    const t_float size) {
            if (node1<node2) {
                *(Z++) = static_cast<t_float>(node1);
                *(Z++) = static_cast<t_float>(node2);
            }
            else {
                *(Z++) = static_cast<t_float>(node2);
                *(Z++) = static_cast<t_float>(node1);
            }
            *(Z++) = dist;
            *(Z++) = size;
        }
    };
    
    class doubly_linked_list {
        /*
         Class for a doubly linked list. Initially, the list is the integer range
         [0, size]. We provide a forward iterator and a method to delete an index
         from the list.
         
         Typical use: for (i=L.start; L<size; i=L.succ[I])
         or
         for (i=somevalue; L<size; i=L.succ[I])
         */
    public:
        t_index start;
        auto_array_ptr<t_index> succ;
        
    private:
        auto_array_ptr<t_index> pred;
        // Not necessarily private, we just do not need it in this instance.
        
    public:
        doubly_linked_list(const t_index size)
        // Initialize to the given size.
        : start(0)
        , succ(size+1)
        , pred(size+1)
        {
            for (t_index i=0; i<size; ++i) {
                pred[i+1] = i;
                succ[i] = i+1;
            }
            // pred[0] is never accessed!
            //succ[size] is never accessed!
        }
        
        ~doubly_linked_list() {}
        
        void remove(const t_index idx) {
            // Remove an index from the list.
            if (idx==start) {
                start = succ[idx];
            }
            else {
                succ[pred[idx]] = succ[idx];
                pred[succ[idx]] = pred[idx];
            }
            succ[idx] = 0; // Mark as inactive
        }
        
        bool is_inactive(t_index idx) const {
            return (succ[idx]==0);
        }
    };
    
    // Indexing functions
    // D is the upper triangular part of a symmetric (NxN)-matrix
    // We require r_ < c_ !
#define D_(r_,c_) ( D[(static_cast<std::ptrdiff_t>(2*N-3-(r_))*(r_)>>1)+(c_)-1] )
    // Z is an ((N-1)x4)-array
#define Z_(_r, _c) (Z[(_r)*4 + (_c)])
    
    /*
     Lookup function for a union-find data structure.
     
     The function finds the root of idx by going iteratively through all
     parent elements until a root is found. An element i is a root if
     nodes[i] is zero. To make subsequent searches faster, the entry for
     idx and all its parents is updated with the root element.
     */
    class union_find {
    private:
        auto_array_ptr<t_index> parent;
        t_index nextparent;
        
    public:
        union_find(const t_index size)
        : parent(size>0 ? 2*size-1 : 0, 0)
        , nextparent(size)
        { }
        
        t_index Find (t_index idx) const {
            if (parent[idx] != 0 ) { // a → b
                t_index p = idx;
                idx = parent[idx];
                if (parent[idx] != 0 ) { // a → b → c
                    do {
                        idx = parent[idx];
                    } while (parent[idx] != 0);
                    do {
                        t_index tmp = parent[p];
                        parent[p] = idx;
                        p = tmp;
                    } while (parent[p] != idx);
                }
            }
            return idx;
        }
        
        void Union (const t_index node1, const t_index node2) {
            parent[node1] = parent[node2] = nextparent++;
        }
    };
    
    class nan_error{};
#ifdef FE_INVALID
    class fenv_error{};
#endif
    
    
    
    /* Functions for the update of the dissimilarity array */
    
    inline static void f_single( t_float * const b, const t_float a ) {
        if (*b > a) *b = a;
    }
    inline static void f_complete( t_float * const b, const t_float a ) {
        if (*b < a) *b = a;
    }
    inline static void f_average( t_float * const b, const t_float a, const t_float s, const t_float t) {
        *b = s*a + t*(*b);
#ifndef FE_INVALID
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
        if (fc_isnan(*b)) {
            throw(nan_error());
        }
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic pop
#endif
#endif
    }
    inline static void f_weighted( t_float * const b, const t_float a) {
        *b = (a+*b)*.5;
#ifndef FE_INVALID
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
        if (fc_isnan(*b)) {
            throw(nan_error());
        }
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic pop
#endif
#endif
    }
    inline static void f_ward( t_float * const b, const t_float a, const t_float c, const t_float s, const t_float t, const t_float v) {
        *b = ( (v+s)*a - v*c + (v+t)*(*b) ) / (s+t+v);
        //*b = a+(*b)-(t*a+s*(*b)+v*c)/(s+t+v);
#ifndef FE_INVALID
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
        if (fc_isnan(*b)) {
            throw(nan_error());
        }
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic pop
#endif
#endif
    }
    inline static void f_centroid( t_float * const b, const t_float a, const t_float stc, const t_float s, const t_float t) {
        *b = s*a - stc + t*(*b);
#ifndef FE_INVALID
        if (fc_isnan(*b)) {
            throw(nan_error());
        }
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic pop
#endif
#endif
    }
    inline static void f_median( t_float * const b, const t_float a, const t_float c_4) {
        *b = (a+(*b))*.5 - c_4;
#ifndef FE_INVALID
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
        if (fc_isnan(*b)) {
            throw(nan_error());
        }
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic pop
#endif
#endif
    }
    
    
    class binary_min_heap {
        /*
         Class for a binary min-heap. The data resides in an array A. The elements of
         A are not changed but two lists I and R of indices are generated which point
         to elements of A and backwards.
         
         The heap tree structure is
         
         H[2*i+1]     H[2*i+2]
         \            /
         \          /
         ≤        ≤
         \      /
         \    /
         H[i]
         
         where the children must be less or equal than their parent. Thus, H[0]
         contains the minimum. The lists I and R are made such that H[i] = A[I[i]]
         and R[I[i]] = i.
         
         This implementation is not designed to handle NaN values.
         */
    private:
        t_float * const A;
        t_index size;
        auto_array_ptr<t_index> I;
        auto_array_ptr<t_index> R;
        
        // no default constructor
        binary_min_heap();
        // noncopyable
        binary_min_heap(binary_min_heap const &);
        binary_min_heap & operator=(binary_min_heap const &);
        
    public:
        binary_min_heap(t_float * const A_, const t_index size_)
        : A(A_), size(size_), I(size), R(size)
        { // Allocate memory and initialize the lists I and R to the identity. This
            // does not make it a heap. Call heapify afterwards!
            for (t_index i=0; i<size; ++i)
                R[i] = I[i] = i;
        }
        
        binary_min_heap(t_float * const A_, const t_index size1, const t_index size2,
                        const t_index start)
        : A(A_), size(size1), I(size1), R(size2)
        { // Allocate memory and initialize the lists I and R to the identity. This
            // does not make it a heap. Call heapify afterwards!
            for (t_index i=0; i<size; ++i) {
                R[i+start] = i;
                I[i] = i + start;
            }
        }
        
        ~binary_min_heap() {}
        
        void heapify() {
            // Arrange the indices I and R so that H[i] := A[I[i]] satisfies the heap
            // condition H[i] < H[2*i+1] and H[i] < H[2*i+2] for each i.
            //
            // Complexity: Θ(size)
            // Reference: Cormen, Leiserson, Rivest, Stein, Introduction to Algorithms,
            // 3rd ed., 2009, Section 6.3 “Building a heap”
            t_index idx;
            for (idx=(size>>1); idx>0; ) {
                --idx;
                update_geq_(idx);
            }
        }
        
        inline t_index argmin() const {
            // Return the minimal element.
            return I[0];
        }
        
        void heap_pop() {
            // Remove the minimal element from the heap.
            --size;
            I[0] = I[size];
            R[I[0]] = 0;
            update_geq_(0);
        }
        
        void remove(t_index idx) {
            // Remove an element from the heap.
            --size;
            R[I[size]] = R[idx];
            I[R[idx]] = I[size];
            if ( H(size)<=A[idx] ) {
                update_leq_(R[idx]);
            }
            else {
                update_geq_(R[idx]);
            }
        }
        
        void replace ( const t_index idxold, const t_index idxnew,
                      const t_float val) {
            R[idxnew] = R[idxold];
            I[R[idxnew]] = idxnew;
            if (val<=A[idxold])
                update_leq(idxnew, val);
            else
                update_geq(idxnew, val);
        }
        
        void update ( const t_index idx, const t_float val ) const {
            // Update the element A[i] with val and re-arrange the indices to preserve
            // the heap condition.
            if (val<=A[idx])
                update_leq(idx, val);
            else
                update_geq(idx, val);
        }
        
        void update_leq ( const t_index idx, const t_float val ) const {
            // Use this when the new value is not more than the old value.
            A[idx] = val;
            update_leq_(R[idx]);
        }
        
        void update_geq ( const t_index idx, const t_float val ) const {
            // Use this when the new value is not less than the old value.
            A[idx] = val;
            update_geq_(R[idx]);
        }
        
    private:
        void update_leq_ (t_index i) const {
            t_index j;
            for ( ; (i>0) && ( H(i)<H(j=(i-1)>>1) ); i=j)
                heap_swap(i,j);
        }
        
        void update_geq_ (t_index i) const {
            t_index j;
            for ( ; (j=2*i+1)<size; i=j) {
                if ( H(j)>=H(i) ) {
                    ++j;
                    if ( j>=size || H(j)>=H(i) ) break;
                }
                else if ( j+1<size && H(j+1)<H(j) ) ++j;
                heap_swap(i, j);
            }
        }
        
        void heap_swap(const t_index i, const t_index j) const {
            // Swap two indices.
            t_index tmp = I[i];
            I[i] = I[j];
            I[j] = tmp;
            R[I[i]] = i;
            R[I[j]] = j;
        }
        
        inline t_float H(const t_index i) const {
            return A[I[i]];
        }
        
    };
    
    /*
     This class handles all the information about the dissimilarity
     computation.
     */
    enum {
        // metrics
        METRIC_EUCLIDEAN       =  0,
        METRIC_MINKOWSKI       =  1,
        METRIC_CITYBLOCK       =  2,
        METRIC_SEUCLIDEAN      =  3,
        METRIC_SQEUCLIDEAN     =  4,
        METRIC_COSINE          =  5,
        METRIC_HAMMING         =  6,
        METRIC_JACCARD         =  7,
        METRIC_CHEBYCHEV       =  8,
        METRIC_CANBERRA        =  9,
        METRIC_BRAYCURTIS      = 10,
        METRIC_MAHALANOBIS     = 11,
        METRIC_YULE            = 12,
        METRIC_MATCHING        = 13,
        METRIC_DICE            = 14,
        METRIC_ROGERSTANIMOTO  = 15,
        METRIC_RUSSELLRAO      = 16,
        METRIC_SOKALSNEATH     = 17,
        METRIC_KULSINSKI       = 18,
        METRIC_USER            = 19,
        METRIC_INVALID         = 20, // sentinel
        METRIC_JACCARD_BOOL    = 21, // separate function for Jaccard metric on
    };                             // Boolean input data
    class python_dissimilarity {
        t_float * Xa;
        std::ptrdiff_t dim; // size_t saves many statis_cast<> in products
        t_index N;
        auto_array_ptr<t_float> Xnew;
        t_index * members;
        void (cluster_result::*postprocessfn) (const t_float) const;
        t_float postprocessarg;
        
        t_float (python_dissimilarity::*distfn) (const t_index, const t_index) const;
        
        auto_array_ptr<t_float> precomputed;
        t_float * precomputed2;
        
        const t_float * V_data;
        
        // noncopyable
        python_dissimilarity();
        python_dissimilarity(python_dissimilarity const &);
        python_dissimilarity & operator=(python_dissimilarity const &);
        
    public:
        // Ignore warning about uninitialized member variables. I know what I am
        // doing here, and some member variables are only used for certain metrics.
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#endif
        python_dissimilarity (t_float * const Xarg,
                              t_index * const members_,
                              const unsigned char method,
                              const unsigned char metric,
                              bool temp_point_array)
        {
            switch (method) {
                case METHOD_METR_SINGLE:
                postprocessfn = NULL; // default
                switch (metric) {
                    case METRIC_EUCLIDEAN:
                    set_euclidean();
                    break;

                    case METRIC_CITYBLOCK:
                    set_cityblock();
                    break;
                    
                    default: // case METRIC_JACCARD_BOOL:
                    distfn = &python_dissimilarity::sqeuclidean<false>;
                }
                break;
                
                case METHOD_METR_WARD:
                postprocessfn = &cluster_result::sqrtdouble;
                break;
                
                default:
                postprocessfn = &cluster_result::sqrt;
            }
        }
        
        ~python_dissimilarity() {
        }
        
        inline t_float operator () (const t_index i, const t_index j) const {
            return (this->*distfn)(i,j);
        }
        
        inline t_float X (const t_index i, const t_index j) const {
            return Xa[i*dim+j];
        }
        
        inline bool Xb (const t_index i, const t_index j) const {
            return  reinterpret_cast<bool *>(Xa)[i*dim+j];
        }
        
        inline t_float * Xptr(const t_index i, const t_index j) const {
            return Xa+i*dim+j;
        }
        
        void merge(const t_index i, const t_index j, const t_index newnode) const {
            t_float const * const Pi = i<N ? Xa+i*dim : Xnew+(i-N)*dim;
            t_float const * const Pj = j<N ? Xa+j*dim : Xnew+(j-N)*dim;
            for(t_index k=0; k<dim; ++k) {
                Xnew[(newnode-N)*dim+k] = (Pi[k]*static_cast<t_float>(members[i]) +
                                           Pj[k]*static_cast<t_float>(members[j])) /
                static_cast<t_float>(members[i]+members[j]);
            }
            members[newnode] = members[i]+members[j];
        }
        
        void merge_weighted(const t_index i, const t_index j, const t_index newnode)
        const {
            t_float const * const Pi = i<N ? Xa+i*dim : Xnew+(i-N)*dim;
            t_float const * const Pj = j<N ? Xa+j*dim : Xnew+(j-N)*dim;
            for(t_index k=0; k<dim; ++k) {
                Xnew[(newnode-N)*dim+k] = (Pi[k]+Pj[k])*.5;
            }
        }
        
        void merge_inplace(const t_index i, const t_index j) const {
            t_float const * const Pi = Xa+i*dim;
            t_float * const Pj = Xa+j*dim;
            for(t_index k=0; k<dim; ++k) {
                Pj[k] = (Pi[k]*static_cast<t_float>(members[i]) +
                         Pj[k]*static_cast<t_float>(members[j])) /
                static_cast<t_float>(members[i]+members[j]);
            }
            members[j] += members[i];
        }
        
        void merge_inplace_weighted(const t_index i, const t_index j) const {
            t_float const * const Pi = Xa+i*dim;
            t_float * const Pj = Xa+j*dim;
            for(t_index k=0; k<dim; ++k) {
                Pj[k] = (Pi[k]+Pj[k])*.5;
            }
        }
        
        void postprocess(cluster_result & Z2) const {
            if (postprocessfn!=NULL) {
                (Z2.*postprocessfn)(postprocessarg);
            }
        }
        
        inline t_float ward(const t_index i, const t_index j) const {
            t_float mi = static_cast<t_float>(members[i]);
            t_float mj = static_cast<t_float>(members[j]);
            return sqeuclidean<true>(i,j)*mi*mj/(mi+mj);
        }
        
        inline t_float ward_initial(const t_index i, const t_index j) const {
            // alias for sqeuclidean
            // Factor 2!!!
            return sqeuclidean<true>(i,j);
        }
        
        // This method must not produce NaN if the input is non-NaN.
        inline static t_float ward_initial_conversion(const t_float min) {
            return min*.5;
        }
        
        inline t_float ward_extended(const t_index i, const t_index j) const {
            t_float mi = static_cast<t_float>(members[i]);
            t_float mj = static_cast<t_float>(members[j]);
            return sqeuclidean_extended(i,j)*mi*mj/(mi+mj);
        }
        
        /* We need two variants of the Euclidean metric: one that does not check
         for a NaN result, which is used for the initial distances, and one which
         does, for the updated distances during the clustering procedure.
         */
        template <const bool check_NaN>
        t_float sqeuclidean(const t_index i, const t_index j) const {
            t_float sum = 0;
            /*
             for (t_index k=0; k<dim; ++k) {
             t_float diff = X(i,k) - X(j,k);
             sum += diff*diff;
             }
             */
            // faster
            t_float const * Pi = Xa+i*dim;
            t_float const * Pj = Xa+j*dim;
            for (t_index k=0; k<dim; ++k) {
                t_float diff = Pi[k] - Pj[k];
                sum += diff*diff;
            }
            if (check_NaN) {
                if (fc_isnan(sum))
                throw(nan_error());
            }
            return sum;
        }
        
        t_float sqeuclidean_extended(const t_index i, const t_index j) const {
            t_float sum = 0;
            t_float const * Pi = i<N ? Xa+i*dim : Xnew+(i-N)*dim; // TBD
            t_float const * Pj = j<N ? Xa+j*dim : Xnew+(j-N)*dim;
            for (t_index k=0; k<dim; ++k) {
                t_float diff = Pi[k] - Pj[k];
                sum += diff*diff;
            }
            if (fc_isnan(sum))
                throw(nan_error());
            return sum;
        }
        
        private:
        
        void set_euclidean() {
            distfn = &python_dissimilarity::sqeuclidean<false>;
            postprocessfn = &cluster_result::sqrt;
        }
        
        void set_cityblock() {
            distfn = &python_dissimilarity::cityblock;
        }
        
        void set_chebychev() {
            distfn = &python_dissimilarity::chebychev;
        }
        
        t_float seuclidean(const t_index i, const t_index j) const {
            t_float sum = 0;
            for (t_index k=0; k<dim; ++k) {
                t_float diff = X(i,k)-X(j,k);
                sum += diff*diff/V_data[k];
            }
            return sum;
        }
        
        t_float cityblock(const t_index i, const t_index j) const {
            t_float sum = 0;
            for (t_index k=0; k<dim; ++k) {
                sum += fabs(X(i,k)-X(j,k));
            }
            return sum;
        }
        
        t_float minkowski(const t_index i, const t_index j) const {
            t_float sum = 0;
            for (t_index k=0; k<dim; ++k) {
                sum += pow(fabs(X(i,k)-X(j,k)),postprocessarg);
            }
            return sum;
        }
        
        t_float chebychev(const t_index i, const t_index j) const {
            t_float max = 0;
            for (t_index k=0; k<dim; ++k) {
                t_float diff = fabs(X(i,k)-X(j,k));
                if (diff>max) {
                    max = diff;
                }
            }
            return max;
        }
        
        t_float cosine(const t_index i, const t_index j) const {
            t_float sum = 0;
            for (t_index k=0; k<dim; ++k) {
                sum -= X(i,k)*X(j,k);
            }
            return sum*precomputed[i]*precomputed[j];
        }
        
        t_float hamming(const t_index i, const t_index j) const {
            t_float sum = 0;
            for (t_index k=0; k<dim; ++k) {
                sum += (X(i,k)!=X(j,k));
            }
            return sum;
        }
        
        // Differs from scipy.spatial.distance: equal vectors correctly
        // return distance 0.
        t_float jaccard(const t_index i, const t_index j) const {
            t_index sum1 = 0;
            t_index sum2 = 0;
            for (t_index k=0; k<dim; ++k) {
                sum1 += (X(i,k)!=X(j,k));
                sum2 += ((X(i,k)!=0) || (X(j,k)!=0));
            }
            return sum1==0 ? 0 : static_cast<t_float>(sum1) / static_cast<t_float>(sum2);
        }
        
        t_float canberra(const t_index i, const t_index j) const {
            t_float sum = 0;
            for (t_index k=0; k<dim; ++k) {
                t_float numerator = fabs(X(i,k)-X(j,k));
                sum += numerator==0 ? 0 : numerator / (fabs(X(i,k)) + fabs(X(j,k)));
            }
            return sum;
        }
        
        t_float braycurtis(const t_index i, const t_index j) const {
            t_float sum1 = 0;
            t_float sum2 = 0;
            for (t_index k=0; k<dim; ++k) {
                sum1 += fabs(X(i,k)-X(j,k));
                sum2 += fabs(X(i,k)+X(j,k));
            }
            return sum1/sum2;
        }
        
        t_float mahalanobis(const t_index i, const t_index j) const {
            // V_data contains the product X*VI
            t_float sum = 0;
            for (t_index k=0; k<dim; ++k) {
                sum += (V_data[i*dim+k]-V_data[j*dim+k])*(X(i,k)-X(j,k));
            }
            return sum;
        }
        
        t_index mutable NTT; // 'local' variables
        t_index mutable NXO;
        t_index mutable NTF;
#define NTFFT NTF
#define NFFTT NTT
        
        void nbool_correspond(const t_index i, const t_index j) const {
            NTT = 0;
            NXO = 0;
            for (t_index k=0; k<dim; ++k) {
                NTT += (Xb(i,k) &  Xb(j,k)) ;
                NXO += (Xb(i,k) ^  Xb(j,k)) ;
            }
        }
        
        void nbool_correspond_tfft(const t_index i, const t_index j) const {
            NTT = 0;
            NXO = 0;
            NTF = 0;
            for (t_index k=0; k<dim; ++k) {
                NTT += (Xb(i,k) &  Xb(j,k)) ;
                NXO += (Xb(i,k) ^  Xb(j,k)) ;
                NTF += (Xb(i,k) & ~Xb(j,k)) ;
            }
            NTF *= (NXO-NTF); // NTFFT
            NTT *= (static_cast<t_index>(dim)-NTT-NXO); // NFFTT
        }
        
        void nbool_correspond_xo(const t_index i, const t_index j) const {
            NXO = 0;
            for (t_index k=0; k<dim; ++k) {
                NXO += (Xb(i,k) ^ Xb(j,k)) ;
            }
        }
        
        void nbool_correspond_tt(const t_index i, const t_index j) const {
            NTT = 0;
            for (t_index k=0; k<dim; ++k) {
                NTT += (Xb(i,k) & Xb(j,k)) ;
            }
        }
        
        // Caution: zero denominators can happen here!
        t_float yule(const t_index i, const t_index j) const {
            nbool_correspond_tfft(i, j);
            return static_cast<t_float>(2*NTFFT) / static_cast<t_float>(NTFFT + NFFTT);
        }
        
        // Prevent a zero denominator for equal vectors.
        t_float dice(const t_index i, const t_index j) const {
            nbool_correspond(i, j);
            return (NXO==0) ? 0 :
            static_cast<t_float>(NXO) / static_cast<t_float>(NXO+2*NTT);
        }
        
        t_float rogerstanimoto(const t_index i, const t_index j) const {
            nbool_correspond_xo(i, j);
            return static_cast<t_float>(2*NXO) / static_cast<t_float>(NXO+dim);
        }
        
        t_float russellrao(const t_index i, const t_index j) const {
            nbool_correspond_tt(i, j);
            return static_cast<t_float>(dim-NTT);
        }
        
        // Prevent a zero denominator for equal vectors.
        t_float sokalsneath(const t_index i, const t_index j) const {
            nbool_correspond(i, j);
            return (NXO==0) ? 0 :
            static_cast<t_float>(2*NXO) / static_cast<t_float>(NTT+2*NXO);
        }
        
        t_float kulsinski(const t_index i, const t_index j) const {
            nbool_correspond_tt(i, j);
            return static_cast<t_float>(NTT) * (precomputed[i] + precomputed[j]);
        }
        
        // 'matching' distance = Hamming distance
        t_float matching(const t_index i, const t_index j) const {
            nbool_correspond_xo(i, j);
            return static_cast<t_float>(NXO);
        }
        
        // Prevent a zero denominator for equal vectors.
        t_float jaccard_bool(const t_index i, const t_index j) const {
            nbool_correspond(i, j);
            return (NXO==0) ? 0 :
            static_cast<t_float>(NXO) / static_cast<t_float>(NXO+NTT);
        }
    };
    
    //double cuttree();
    
    
    void MST_linkage_core(const t_index N, const t_float * const D,
                          cluster_result & Z2);
    
    template <const unsigned char method, typename t_members>
    void NN_chain_core(const t_index N, t_float * const D, t_members * const members, cluster_result & Z2)
    {
        /*
         N: integer
         D: condensed distance matrix N*(N-1)/2
         Z2: output data structure
         
         This is the NN-chain algorithm, described on page 86 in the following book:
         
         Fionn Murtagh, Multidimensional Clustering Algorithms,
         Vienna, Würzburg: Physica-Verlag, 1985.
         */
        t_index i;
        
        auto_array_ptr<t_index> NN_chain(N);
        t_index NN_chain_tip = 0;
        
        t_index idx1, idx2;
        
        t_float size1, size2;
        doubly_linked_list active_nodes(N);
        
        t_float min;
        
        for (t_float const * DD=D; DD!=D+(static_cast<std::ptrdiff_t>(N)*(N-1)>>1);
             ++DD) {
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
            if (fc_isnan(*DD)) {
                throw(nan_error());
            }
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic pop
#endif
        }
        
#ifdef FE_INVALID
        if (feclearexcept(FE_INVALID)) throw fenv_error();
#endif
        
        for (t_index j=0; j<N-1; ++j) {
            // set a point (arbitrarily)
            // grow the NN-chain from the object chosen, until a pair of RNNs
            // are obtained: i ->NN(i)=j -> NN(j)=k ->...->
            if (NN_chain_tip <= 3) {
                NN_chain[0] = idx1 = active_nodes.start;
                NN_chain_tip = 1;
                
                idx2 = active_nodes.succ[idx1];
                min = D_(idx1,idx2);
                for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                    if (D_(idx1,i) < min) {
                        min = D_(idx1,i);
                        idx2 = i;
                    }
                }
            }  // a: idx1   b: idx2
            else {
                NN_chain_tip -= 3;
                idx1 = NN_chain[NN_chain_tip-1];
                idx2 = NN_chain[NN_chain_tip];
                min = idx1<idx2 ? D_(idx1,idx2) : D_(idx2,idx1);
            }  // a: idx1   b: idx2
            
            do {
                NN_chain[NN_chain_tip] = idx2;
                
                for (i=active_nodes.start; i<idx2; i=active_nodes.succ[i]) {
                    if (D_(i,idx2) < min) {
                        min = D_(i,idx2);
                        idx1 = i;
                    }
                }
                for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                    if (D_(idx2,i) < min) {
                        min = D_(idx2,i);
                        idx1 = i;
                    }
                }
                
                idx2 = idx1;
                idx1 = NN_chain[NN_chain_tip++];
                
            } while (idx2 != NN_chain[NN_chain_tip-2]);
            
            // agglomerate these objects
            Z2.append(idx1, idx2, min);
            
            if (idx1>idx2) {
                t_index tmp = idx1;
                idx1 = idx2;
                idx2 = tmp;
            }
            
            // update the dissimilarity table
            if (method==METHOD_METR_AVERAGE ||
                method==METHOD_METR_WARD) {
                size1 = static_cast<t_float>(members[idx1]);
                size2 = static_cast<t_float>(members[idx2]);
                members[idx2] += members[idx1];
            }
            
            // Remove the smaller index from the valid indices (active_nodes).
            active_nodes.remove(idx1);
            
            switch (method) {
                case METHOD_METR_SINGLE:
                    /*
                     Single linkage.
                     
                     Characteristic: new distances are never longer than the old distances.
                     */
                    // Update the distance matrix in the range [start, idx1).
                    for (i=active_nodes.start; i<idx1; i=active_nodes.succ[i]) {
                        if (D_(i, idx2) != DBL_MAX && D_(i, idx1) != DBL_MAX) {
                            f_single(&D_(i, idx2), D_(i, idx1) );
                        }
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for (; i<idx2; i=active_nodes.succ[i]) {
                        if (D_(i, idx2) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                            f_single(&D_(i, idx2), D_(idx1, i) );
                        }
                    }
                    // Update the distance matrix in the range (idx2, N).
                    for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                        if (D_(idx2, i) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                            f_single(&D_(idx2, i), D_(idx1, i) );
                        }
                    }
                    break;
                    
                case METHOD_METR_COMPLETE:
                    /*
                     Complete linkage.
                     
                     Characteristic: new distances are never shorter than the old distances.
                     */
                    // Update the distance matrix in the range [start, idx1).
                    for (i=active_nodes.start; i<idx1; i=active_nodes.succ[i]) {
                        if (D_(i, idx2) != DBL_MAX && D_(i, idx1) != DBL_MAX) {
                            f_complete(&D_(i, idx2), D_(i, idx1) );
                        }
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for (; i<idx2; i=active_nodes.succ[i]) {
                        if (D_(i, idx2) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                            f_complete(&D_(i, idx2), D_(idx1, i) );
                        }
                    }
                    // Update the distance matrix in the range (idx2, N).
                    for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                        if (D_(idx2, i) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                            f_complete(&D_(idx2, i), D_(idx1, i) );
                        }
                    }
                    break;
                    
                case METHOD_METR_AVERAGE: {
                    /*
                     Average linkage.
                     
                     Shorter and longer distances can occur.
                     */
                    // Update the distance matrix in the range [start, idx1).
                    t_float s = size1/(size1+size2);
                    t_float t = size2/(size1+size2);
                    for (i=active_nodes.start; i<idx1; i=active_nodes.succ[i]) {
                        if (D_(i, idx2) != DBL_MAX && D_(i, idx1) != DBL_MAX) {
                            f_average(&D_(i, idx2), D_(i, idx1), s, t );
                        }
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for (; i<idx2; i=active_nodes.succ[i]) {
                        if (D_(i, idx2) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                            f_average(&D_(i, idx2), D_(idx1, i), s, t );
                        }
                    }
                    // Update the distance matrix in the range (idx2, N).
                    for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                        if (D_(idx2, i) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                            f_average(&D_(idx2, i), D_(idx1, i), s, t );
                        }
                    }
                    break;
                }
                    
                case METHOD_METR_WEIGHTED:
                    /*
                     Weighted linkage.
                     
                     Shorter and longer distances can occur.
                     */
                    // Update the distance matrix in the range [start, idx1).
                    for (i=active_nodes.start; i<idx1; i=active_nodes.succ[i]) {
                        if (D_(i, idx2) != DBL_MAX && D_(i, idx1) != DBL_MAX) {
                            f_weighted(&D_(i, idx2), D_(i, idx1) );
                        }
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for (; i<idx2; i=active_nodes.succ[i]) {
                        if (D_(i, idx2) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                            f_weighted(&D_(i, idx2), D_(idx1, i) );
                        }
                    }
                    // Update the distance matrix in the range (idx2, N).
                    for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                        if (D_(idx2, i) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                            f_weighted(&D_(idx2, i), D_(idx1, i) );
                        }
                    }
                    break;
                    
                case METHOD_METR_WARD:
                    /*
                     Ward linkage.
                     
                     Shorter and longer distances can occur, not smaller than min(d1,d2)
                     but maybe bigger than max(d1,d2).
                     */
                    // Update the distance matrix in the range [start, idx1).
                    //t_float v = static_cast<t_float>(members[i]);
                    for (i=active_nodes.start; i<idx1; i=active_nodes.succ[i]) {
                        if (D_(i, idx2) != DBL_MAX && D_(i, idx1) != DBL_MAX) {
                            f_ward(&D_(i, idx2), D_(i, idx1), min,
                               size1, size2, static_cast<t_float>(members[i]) );
                        }
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for (; i<idx2; i=active_nodes.succ[i]) {
                        if (D_(i, idx2) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                            f_ward(&D_(i, idx2), D_(idx1, i), min,
                               size1, size2, static_cast<t_float>(members[i]) );
                        }
                    }
                    // Update the distance matrix in the range (idx2, N).
                    for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                        if (D_(idx2, i) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                            f_ward(&D_(idx2, i), D_(idx1, i), min,
                               size1, size2, static_cast<t_float>(members[i]) );
                        }
                    }
                    break;
                    
                default:
                    throw std::runtime_error(std::string("Invalid method."));
            }
        }
#ifdef FE_INVALID
        if (fetestexcept(FE_INVALID)) throw fenv_error();
#endif
    }

    bool CheckContiguity(GalElement* w, t_index idx1, t_index idx2, std::map<std::pair<t_index, t_index>, bool>  & conn_dict);

    template <const unsigned char method, typename t_members>
        void NN_chain_core_w(GalElement* w, const t_index N, t_float * const D, t_members * const members, cluster_result & Z2)
        {
            std::map<std::pair<t_index, t_index>, bool> conn_dict;
            /*
             N: integer
             D: condensed distance matrix N*(N-1)/2
             Z2: output data structure
             
             This is the NN-chain algorithm, described on page 86 in the following book:
             
             Fionn Murtagh, Multidimensional Clustering Algorithms,
             Vienna, Würzburg: Physica-Verlag, 1985.
             */
            t_index i;
            
            auto_array_ptr<t_index> NN_chain(N);
            t_index NN_chain_tip = 0;
            
            t_index idx1, idx2;
            
            t_float size1, size2;
            doubly_linked_list active_nodes(N);
            
            t_float min;
            
            for (t_float const * DD=D; DD!=D+(static_cast<std::ptrdiff_t>(N)*(N-1)>>1);
                 ++DD) {
                if (fc_isnan(*DD)) {
                    throw(nan_error());
                }
            }
            
            for (t_index j=0; j<N-1; ++j) {
                // set a point (arbitrarily)
                // grow the NN-chain from the object chosen, until a pair of RNNs
                // are obtained: i ->NN(i)=j -> NN(j)=k ->...->
                
                bool  singleton = false;
                
                if (NN_chain_tip <= 3) {
                    idx1 = active_nodes.start;
                    NN_chain[0] = active_nodes.start;
                    NN_chain_tip = 1; // find next in the chain
                    
                    // NN of idx1
                    idx2 = active_nodes.succ[idx1];
                    min = DBL_MAX;
                    if (CheckContiguity(w, idx1, idx2, conn_dict)) {
                        min = D_(idx1,idx2);
                    }
                    for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                        if (D_(idx1,i) < min &&  CheckContiguity(w, idx1, i, conn_dict)) {
                            min = D_(idx1,i);
                            idx2 = i;
                        }
                    }
                    if (min == DBL_MAX) {
                        // not found
                        singleton = true;
                    }
                }  // a: idx1   b: idx2
                else {
                    // o->p<->q
                    // (p,q) will be merged, so go back to -3 index and continue search
                    NN_chain_tip -= 3;
                    idx1 = NN_chain[NN_chain_tip-1];
                    idx2 = NN_chain[NN_chain_tip];
                    min = idx1<idx2 ? D_(idx1,idx2) : D_(idx2,idx1);
                    if (!CheckContiguity(w, idx1, idx2, conn_dict)) {
                        std::cout << "not conn 0" << std::endl;
                    }
                }  // a: idx1   b: idx2
                
                if (singleton) {
                    // can't find contiguity pair for idx1
                    // assign idx1 to a neareast cluster
                    const std::vector<long>& nbrs = w[idx1].GetNbrs();
                    for (int i=0; i<nbrs.size(); i++ ) {
                        t_index nid = nbrs[i];
                        if (conn_dict[std::make_pair(idx1, nid)] == false) {
                            double _min = idx1<nid ? D_(idx1,nid) : D_(nid,idx1);
                            if (_min < min) {
                                min = _min;
                                idx2 = nid;
                                //singleton = false;
                            }
                        }
                    }

                } else {
                    do {
                        NN_chain[NN_chain_tip] = idx2;
                        
                        bool found = false;
                        
                        // then find NN of idx2
                        for (i=active_nodes.start; i<idx2; i=active_nodes.succ[i]) {
                            if (i!= idx1 && D_(i,idx2) < min &&  CheckContiguity(w, i, idx2, conn_dict)) {
                                min = D_(i,idx2);
                                idx1 = i;
                                found = true;
                            }
                        }
                        for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                            if (D_(idx2,i) < min  &&  CheckContiguity(w, i, idx2, conn_dict)) {
                                min = D_(idx2,i);
                                idx1 = i;
                                found = true;
                            }
                        }
                        
                        if (found) {
                            idx2 = idx1;
                            idx1 = NN_chain[NN_chain_tip++];
                        } else {
                            NN_chain_tip++;
                            break;
                        }
                        
                        if (!CheckContiguity(w, idx1, idx2, conn_dict)) {
                            std::cout << "not conn 1" << std::endl;
                        }
                    } while (idx2 != NN_chain[NN_chain_tip-2]);
                }
                // idx1 and idx2 are RNN
                // merge the two points
                Z2.append(idx1, idx2, min);
                std::cout << "merge:" << idx1 << "," <<idx2 << "," << min << std::endl;
                
                if (!CheckContiguity(w, idx1, idx2, conn_dict)) {
                    std::cout << "not conn 2" << std::endl;
                }
                
                conn_dict[std::make_pair(idx1, idx2)] = true;
                conn_dict[std::make_pair(idx2, idx1)] = true;
                
                if (idx1>idx2) {
                    t_index tmp = idx1;
                    idx1 = idx2;
                    idx2 = tmp;
                }
                
                // update the dissimilarity table
                if (method==METHOD_METR_AVERAGE ||
                    method==METHOD_METR_WARD) {
                    size1 = static_cast<t_float>(members[idx1]);
                    size2 = static_cast<t_float>(members[idx2]);
                    members[idx2] += members[idx1];
                }
                
                // Remove the smaller index from the valid indices (active_nodes).
                active_nodes.remove(idx1);
                
                switch (method) {
                    case METHOD_METR_SINGLE:
                        /*
                         Single linkage.
                         
                         Characteristic: new distances are never longer than the old distances.
                         */
                        // Update the distance matrix in the range [start, idx1).
                        for (i=active_nodes.start; i<idx1; i=active_nodes.succ[i]) {
                            if (D_(i, idx2) != DBL_MAX && D_(i, idx1) != DBL_MAX) {
                                f_single(&D_(i, idx2), D_(i, idx1) );
                            }
                        }
                        // Update the distance matrix in the range (idx1, idx2).
                        for (; i<idx2; i=active_nodes.succ[i]) {
                            if (D_(i, idx2) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                                f_single(&D_(i, idx2), D_(idx1, i) );
                            }
                        }
                        // Update the distance matrix in the range (idx2, N).
                        for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                            if (D_(idx2, i) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                                f_single(&D_(idx2, i), D_(idx1, i) );
                            }
                        }
                        break;
                        
                    case METHOD_METR_COMPLETE:
                        /*
                         Complete linkage.
                         
                         Characteristic: new distances are never shorter than the old distances.
                         */
                        // Update the distance matrix in the range [start, idx1).
                        for (i=active_nodes.start; i<idx1; i=active_nodes.succ[i]) {
                            if (D_(i, idx2) != DBL_MAX && D_(i, idx1) != DBL_MAX) {
                                f_complete(&D_(i, idx2), D_(i, idx1) );
                            }
                        }
                        // Update the distance matrix in the range (idx1, idx2).
                        for (; i<idx2; i=active_nodes.succ[i]) {
                            if (D_(i, idx2) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                                f_complete(&D_(i, idx2), D_(idx1, i) );
                            }
                        }
                        // Update the distance matrix in the range (idx2, N).
                        for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                            if (D_(idx2, i) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                                f_complete(&D_(idx2, i), D_(idx1, i) );
                            }
                        }
                        break;
                        
                    case METHOD_METR_AVERAGE: {
                        /*
                         Average linkage.
                         
                         Shorter and longer distances can occur.
                         */
                        // Update the distance matrix in the range [start, idx1).
                        t_float s = size1/(size1+size2);
                        t_float t = size2/(size1+size2);
                        for (i=active_nodes.start; i<idx1; i=active_nodes.succ[i]) {
                            if (D_(i, idx2) != DBL_MAX && D_(i, idx1) != DBL_MAX) {
                                f_average(&D_(i, idx2), D_(i, idx1), s, t );
                            }
                        }
                        // Update the distance matrix in the range (idx1, idx2).
                        for (; i<idx2; i=active_nodes.succ[i]) {
                            if (D_(i, idx2) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                                f_average(&D_(i, idx2), D_(idx1, i), s, t );
                            }
                        }
                        // Update the distance matrix in the range (idx2, N).
                        for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                            if (D_(idx2, i) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                                f_average(&D_(idx2, i), D_(idx1, i), s, t );
                            }
                        }
                        break;
                    }
                        
                    case METHOD_METR_WEIGHTED:
                        /*
                         Weighted linkage.
                         
                         Shorter and longer distances can occur.
                         */
                        // Update the distance matrix in the range [start, idx1).
                        for (i=active_nodes.start; i<idx1; i=active_nodes.succ[i]) {
                            if (D_(i, idx2) != DBL_MAX && D_(i, idx1) != DBL_MAX) {
                                f_weighted(&D_(i, idx2), D_(i, idx1) );
                            }
                        }
                        // Update the distance matrix in the range (idx1, idx2).
                        for (; i<idx2; i=active_nodes.succ[i]) {
                            if (D_(i, idx2) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                                f_weighted(&D_(i, idx2), D_(idx1, i) );
                            }
                        }
                        // Update the distance matrix in the range (idx2, N).
                        for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                            if (D_(idx2, i) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                                f_weighted(&D_(idx2, i), D_(idx1, i) );
                            }
                        }
                        break;
                        
                    case METHOD_METR_WARD:
                        /*
                         Ward linkage.
                         
                         Shorter and longer distances can occur, not smaller than min(d1,d2)
                         but maybe bigger than max(d1,d2).
                         */
                        // Update the distance matrix in the range [start, idx1).
                        //t_float v = static_cast<t_float>(members[i]);
                        for (i=active_nodes.start; i<idx1; i=active_nodes.succ[i]) {
                            if (D_(i, idx2) != DBL_MAX && D_(i, idx1) != DBL_MAX) {
                                f_ward(&D_(i, idx2), D_(i, idx1), min,
                                   size1, size2, static_cast<t_float>(members[i]) );
                            }
                        }
                        // Update the distance matrix in the range (idx1, idx2).
                        for (; i<idx2; i=active_nodes.succ[i]) {
                            if (D_(i, idx2) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                                f_ward(&D_(i, idx2), D_(idx1, i), min,
                                   size1, size2, static_cast<t_float>(members[i]) );
                            }
                        }
                        // Update the distance matrix in the range (idx2, N).
                        for (i=active_nodes.succ[idx2]; i<N; i=active_nodes.succ[i]) {
                            if (D_(idx2, i) != DBL_MAX && D_(idx1, i) != DBL_MAX) {
                                f_ward(&D_(idx2, i), D_(idx1, i), min,
                                   size1, size2, static_cast<t_float>(members[i]) );
                            }
                        }
                        break;
                        
                    default:
                        throw std::runtime_error(std::string("Invalid method."));
                }
                if (singleton == true) {
                    // something wrong is here!
                    active_nodes.remove(idx2);
                }
            }
        }
    
    template <const unsigned char method, typename t_members>
    void generic_linkage(const t_index N, t_float * const D, t_members * const members, cluster_result & Z2)
    {
        /*
         N: integer, number of data points
         D: condensed distance matrix N*(N-1)/2
         Z2: output data structure
         */
        
        const t_index N_1 = N-1;
        t_index i, j; // loop variables
        t_index idx1, idx2; // row and column indices
        
        auto_array_ptr<t_index> n_nghbr(N_1); // array of nearest neighbors
        auto_array_ptr<t_float> mindist(N_1); // distances to the nearest neighbors
        auto_array_ptr<t_index> row_repr(N); // row_repr[i]: node number that the
        // i-th row represents
        doubly_linked_list active_nodes(N);
        binary_min_heap nn_distances(&*mindist, N_1); // minimum heap structure for
        // the distance to the nearest neighbor of each point
        t_index node1, node2; // node numbers in the output
        t_float size1, size2; // and their cardinalities
        
        t_float min; // minimum and row index for nearest-neighbor search
        t_index idx;
        
        for (i=0; i<N; ++i)
            // Build a list of row ↔ node label assignments.
            // Initially i ↦ i
            row_repr[i] = i;
        
        // Initialize the minimal distances:
        // Find the nearest neighbor of each point.
        // n_nghbr[i] = argmin_{j>i} D(i,j) for i in range(N-1)
        t_float const * DD = D;
        for (i=0; i<N_1; ++i) {
            min = std::numeric_limits<t_float>::infinity();
            for (idx=j=i+1; j<N; ++j, ++DD) {
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
                if (*DD<min) {
                    min = *DD;
                    idx = j;
                }
                else if (fc_isnan(*DD))
                    throw(nan_error());
            }
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic pop
#endif
            mindist[i] = min;
            n_nghbr[i] = idx;
        }
        
        // Put the minimal distances into a heap structure to make the repeated
        // global minimum searches fast.
        nn_distances.heapify();
        
#ifdef FE_INVALID
        if (feclearexcept(FE_INVALID)) throw fenv_error();
#endif
        
        // Main loop: We have N-1 merging steps.
        for (i=0; i<N_1; ++i) {
            /*
             Here is a special feature that allows fast bookkeeping and updates of the
             minimal distances.
             
             mindist[i] stores a lower bound on the minimum distance of the point i to
             all points of higher index:
             
             mindist[i] ≥ min_{j>i} D(i,j)
             
             Normally, we have equality. However, this minimum may become invalid due
             to the updates in the distance matrix. The rules are:
             
             1) If mindist[i] is equal to D(i, n_nghbr[i]), this is the correct
             minimum and n_nghbr[i] is a nearest neighbor.
             
             2) If mindist[i] is smaller than D(i, n_nghbr[i]), this might not be the
             correct minimum. The minimum needs to be recomputed.
             
             3) mindist[i] is never bigger than the true minimum. Hence, we never
             miss the true minimum if we take the smallest mindist entry,
             re-compute the value if necessary (thus maybe increasing it) and
             looking for the now smallest mindist entry until a valid minimal
             entry is found. This step is done in the lines below.
             
             The update process for D below takes care that these rules are
             fulfilled. This makes sure that the minima in the rows D(i,i+1:)of D are
             re-calculated when necessary but re-calculation is avoided whenever
             possible.
             
             The re-calculation of the minima makes the worst-case runtime of this
             algorithm cubic in N. We avoid this whenever possible, and in most cases
             the runtime appears to be quadratic.
             */
            idx1 = nn_distances.argmin();
            if (method != METHOD_METR_SINGLE) {
                while ( mindist[idx1] < D_(idx1, n_nghbr[idx1]) ) {
                    // Recompute the minimum mindist[idx1] and n_nghbr[idx1].
                    n_nghbr[idx1] = j = active_nodes.succ[idx1]; // exists, maximally N-1
                    min = D_(idx1,j);
                    for (j=active_nodes.succ[j]; j<N; j=active_nodes.succ[j]) {
                        if (D_(idx1,j)<min) {
                            min = D_(idx1,j);
                            n_nghbr[idx1] = j;
                        }
                    }
                    /* Update the heap with the new true minimum and search for the
                     (possibly different) minimal entry. */
                    nn_distances.update_geq(idx1, min);
                    idx1 = nn_distances.argmin();
                }
            }
            
            nn_distances.heap_pop(); // Remove the current minimum from the heap.
            idx2 = n_nghbr[idx1];
            
            // Write the newly found minimal pair of nodes to the output array.
            node1 = row_repr[idx1];
            node2 = row_repr[idx2];
            
            if (method==METHOD_METR_AVERAGE ||
                method==METHOD_METR_WARD ||
                method==METHOD_METR_CENTROID) {
                size1 = static_cast<t_float>(members[idx1]);
                size2 = static_cast<t_float>(members[idx2]);
                members[idx2] += members[idx1];
            }
            Z2.append(node1, node2, mindist[idx1]);
            
            // Remove idx1 from the list of active indices (active_nodes).
            active_nodes.remove(idx1);
            // Index idx2 now represents the new (merged) node with label N+i.
            row_repr[idx2] = N+i;
            
            // Update the distance matrix
            switch (method) {
                case METHOD_METR_SINGLE:
                    /*
                     Single linkage.
                     
                     Characteristic: new distances are never longer than the old distances.
                     */
                    // Update the distance matrix in the range [start, idx1).
                    for (j=active_nodes.start; j<idx1; j=active_nodes.succ[j]) {
                        f_single(&D_(j, idx2), D_(j, idx1));
                        if (n_nghbr[j] == idx1)
                            n_nghbr[j] = idx2;
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for (; j<idx2; j=active_nodes.succ[j]) {
                        f_single(&D_(j, idx2), D_(idx1, j));
                        // If the new value is below the old minimum in a row, update
                        // the mindist and n_nghbr arrays.
                        if (D_(j, idx2) < mindist[j]) {
                            nn_distances.update_leq(j, D_(j, idx2));
                            n_nghbr[j] = idx2;
                        }
                    }
                    // Update the distance matrix in the range (idx2, N).
                    // Recompute the minimum mindist[idx2] and n_nghbr[idx2].
                    if (idx2<N_1) {
                        min = mindist[idx2];
                        for (j=active_nodes.succ[idx2]; j<N; j=active_nodes.succ[j]) {
                            f_single(&D_(idx2, j), D_(idx1, j) );
                            if (D_(idx2, j) < min) {
                                n_nghbr[idx2] = j;
                                min = D_(idx2, j);
                            }
                        }
                        nn_distances.update_leq(idx2, min);
                    }
                    break;
                    
                case METHOD_METR_COMPLETE:
                    /*
                     Complete linkage.
                     
                     Characteristic: new distances are never shorter than the old distances.
                     */
                    // Update the distance matrix in the range [start, idx1).
                    for (j=active_nodes.start; j<idx1; j=active_nodes.succ[j]) {
                        f_complete(&D_(j, idx2), D_(j, idx1) );
                        if (n_nghbr[j] == idx1)
                            n_nghbr[j] = idx2;
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for (; j<idx2; j=active_nodes.succ[j])
                        f_complete(&D_(j, idx2), D_(idx1, j) );
                    // Update the distance matrix in the range (idx2, N).
                    for (j=active_nodes.succ[idx2]; j<N; j=active_nodes.succ[j])
                        f_complete(&D_(idx2, j), D_(idx1, j) );
                    break;
                    
                case METHOD_METR_AVERAGE: {
                    /*
                     Average linkage.
                     
                     Shorter and longer distances can occur.
                     */
                    // Update the distance matrix in the range [start, idx1).
                    t_float s = size1/(size1+size2);
                    t_float t = size2/(size1+size2);
                    for (j=active_nodes.start; j<idx1; j=active_nodes.succ[j]) {
                        f_average(&D_(j, idx2), D_(j, idx1), s, t);
                        if (n_nghbr[j] == idx1)
                            n_nghbr[j] = idx2;
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for (; j<idx2; j=active_nodes.succ[j]) {
                        f_average(&D_(j, idx2), D_(idx1, j), s, t);
                        if (D_(j, idx2) < mindist[j]) {
                            nn_distances.update_leq(j, D_(j, idx2));
                            n_nghbr[j] = idx2;
                        }
                    }
                    // Update the distance matrix in the range (idx2, N).
                    if (idx2<N_1) {
                        n_nghbr[idx2] = j = active_nodes.succ[idx2]; // exists, maximally N-1
                        f_average(&D_(idx2, j), D_(idx1, j), s, t);
                        min = D_(idx2,j);
                        for (j=active_nodes.succ[j]; j<N; j=active_nodes.succ[j]) {
                            f_average(&D_(idx2, j), D_(idx1, j), s, t);
                            if (D_(idx2,j) < min) {
                                min = D_(idx2,j);
                                n_nghbr[idx2] = j;
                            }
                        }
                        nn_distances.update(idx2, min);
                    }
                    break;
                }
                    
                case METHOD_METR_WEIGHTED:
                    /*
                     Weighted linkage.
                     
                     Shorter and longer distances can occur.
                     */
                    // Update the distance matrix in the range [start, idx1).
                    for (j=active_nodes.start; j<idx1; j=active_nodes.succ[j]) {
                        f_weighted(&D_(j, idx2), D_(j, idx1) );
                        if (n_nghbr[j] == idx1)
                            n_nghbr[j] = idx2;
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for (; j<idx2; j=active_nodes.succ[j]) {
                        f_weighted(&D_(j, idx2), D_(idx1, j) );
                        if (D_(j, idx2) < mindist[j]) {
                            nn_distances.update_leq(j, D_(j, idx2));
                            n_nghbr[j] = idx2;
                        }
                    }
                    // Update the distance matrix in the range (idx2, N).
                    if (idx2<N_1) {
                        n_nghbr[idx2] = j = active_nodes.succ[idx2]; // exists, maximally N-1
                        f_weighted(&D_(idx2, j), D_(idx1, j) );
                        min = D_(idx2,j);
                        for (j=active_nodes.succ[j]; j<N; j=active_nodes.succ[j]) {
                            f_weighted(&D_(idx2, j), D_(idx1, j) );
                            if (D_(idx2,j) < min) {
                                min = D_(idx2,j);
                                n_nghbr[idx2] = j;
                            }
                        }
                        nn_distances.update(idx2, min);
                    }
                    break;
                    
                case METHOD_METR_WARD:
                    /*
                     Ward linkage.
                     
                     Shorter and longer distances can occur, not smaller than min(d1,d2)
                     but maybe bigger than max(d1,d2).
                     */
                    // Update the distance matrix in the range [start, idx1).
                    for (j=active_nodes.start; j<idx1; j=active_nodes.succ[j]) {
                        f_ward(&D_(j, idx2), D_(j, idx1), mindist[idx1],
                               size1, size2, static_cast<t_float>(members[j]) );
                        if (n_nghbr[j] == idx1)
                            n_nghbr[j] = idx2;
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for (; j<idx2; j=active_nodes.succ[j]) {
                        f_ward(&D_(j, idx2), D_(idx1, j), mindist[idx1], size1, size2,
                               static_cast<t_float>(members[j]) );
                        if (D_(j, idx2) < mindist[j]) {
                            nn_distances.update_leq(j, D_(j, idx2));
                            n_nghbr[j] = idx2;
                        }
                    }
                    // Update the distance matrix in the range (idx2, N).
                    if (idx2<N_1) {
                        n_nghbr[idx2] = j = active_nodes.succ[idx2]; // exists, maximally N-1
                        f_ward(&D_(idx2, j), D_(idx1, j), mindist[idx1],
                               size1, size2, static_cast<t_float>(members[j]) );
                        min = D_(idx2,j);
                        for (j=active_nodes.succ[j]; j<N; j=active_nodes.succ[j]) {
                            f_ward(&D_(idx2, j), D_(idx1, j), mindist[idx1],
                                   size1, size2, static_cast<t_float>(members[j]) );
                            if (D_(idx2,j) < min) {
                                min = D_(idx2,j);
                                n_nghbr[idx2] = j;
                            }
                        }
                        nn_distances.update(idx2, min);
                    }
                    break;
                    
                case METHOD_METR_CENTROID: {
                    /*
                     Centroid linkage.
                     
                     Shorter and longer distances can occur, not bigger than max(d1,d2)
                     but maybe smaller than min(d1,d2).
                     */
                    // Update the distance matrix in the range [start, idx1).
                    t_float s = size1/(size1+size2);
                    t_float t = size2/(size1+size2);
                    t_float stc = s*t*mindist[idx1];
                    for (j=active_nodes.start; j<idx1; j=active_nodes.succ[j]) {
                        f_centroid(&D_(j, idx2), D_(j, idx1), stc, s, t);
                        if (D_(j, idx2) < mindist[j]) {
                            nn_distances.update_leq(j, D_(j, idx2));
                            n_nghbr[j] = idx2;
                        }
                        else if (n_nghbr[j] == idx1)
                            n_nghbr[j] = idx2;
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for (; j<idx2; j=active_nodes.succ[j]) {
                        f_centroid(&D_(j, idx2), D_(idx1, j), stc, s, t);
                        if (D_(j, idx2) < mindist[j]) {
                            nn_distances.update_leq(j, D_(j, idx2));
                            n_nghbr[j] = idx2;
                        }
                    }
                    // Update the distance matrix in the range (idx2, N).
                    if (idx2<N_1) {
                        n_nghbr[idx2] = j = active_nodes.succ[idx2]; // exists, maximally N-1
                        f_centroid(&D_(idx2, j), D_(idx1, j), stc, s, t);
                        min = D_(idx2,j);
                        for (j=active_nodes.succ[j]; j<N; j=active_nodes.succ[j]) {
                            f_centroid(&D_(idx2, j), D_(idx1, j), stc, s, t);
                            if (D_(idx2,j) < min) {
                                min = D_(idx2,j);
                                n_nghbr[idx2] = j;
                            }
                        }
                        nn_distances.update(idx2, min);
                    }
                    break;
                }
                    
                case METHOD_METR_MEDIAN: {
                    /*
                     Median linkage.
                     
                     Shorter and longer distances can occur, not bigger than max(d1,d2)
                     but maybe smaller than min(d1,d2).
                     */
                    // Update the distance matrix in the range [start, idx1).
                    t_float c_4 = mindist[idx1]*.25;
                    for (j=active_nodes.start; j<idx1; j=active_nodes.succ[j]) {
                        f_median(&D_(j, idx2), D_(j, idx1), c_4 );
                        if (D_(j, idx2) < mindist[j]) {
                            nn_distances.update_leq(j, D_(j, idx2));
                            n_nghbr[j] = idx2;
                        }
                        else if (n_nghbr[j] == idx1)
                            n_nghbr[j] = idx2;
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for (; j<idx2; j=active_nodes.succ[j]) {
                        f_median(&D_(j, idx2), D_(idx1, j), c_4 );
                        if (D_(j, idx2) < mindist[j]) {
                            nn_distances.update_leq(j, D_(j, idx2));
                            n_nghbr[j] = idx2;
                        }
                    }
                    // Update the distance matrix in the range (idx2, N).
                    if (idx2<N_1) {
                        n_nghbr[idx2] = j = active_nodes.succ[idx2]; // exists, maximally N-1
                        f_median(&D_(idx2, j), D_(idx1, j), c_4 );
                        min = D_(idx2,j);
                        for (j=active_nodes.succ[j]; j<N; j=active_nodes.succ[j]) {
                            f_median(&D_(idx2, j), D_(idx1, j), c_4 );
                            if (D_(idx2,j) < min) {
                                min = D_(idx2,j);
                                n_nghbr[idx2] = j;
                            }
                        }
                        nn_distances.update(idx2, min);
                    }
                    break;
                }
                    
                default:
                    throw std::runtime_error(std::string("Invalid method."));
            }
        }
#ifdef FE_INVALID
        if (fetestexcept(FE_INVALID)) throw fenv_error();
#endif
    }
    
    
    /*
     Clustering methods for vector data
     */
    
    template <typename t_dissimilarity>
    void MST_linkage_core_vector(const t_index N,
                                 t_dissimilarity & dist,
                                 cluster_result & Z2)
    {
        /*
         N: integer, number of data points
         dist: function pointer to the metric
         Z2: output data structure
         
         The basis of this algorithm is an algorithm by Rohlf:
         
         F. James Rohlf, Hierarchical clustering using the minimum spanning tree,
         The Computer Journal, vol. 16, 1973, p. 93–95.
         */
        t_index i;
        t_index idx2;
        doubly_linked_list active_nodes(N);
        auto_array_ptr<t_float> d(N);
        
        t_index prev_node;
        t_float min;
        
        // first iteration
        idx2 = 1;
        min = std::numeric_limits<t_float>::infinity();
        for (i=1; i<N; ++i) {
            d[i] = dist(0,i);
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
            if (d[i] < min) {
                min = d[i];
                idx2 = i;
            }
            else if (fc_isnan(d[i]))
                throw (nan_error());
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic pop
#endif
        }
        
        Z2.append(0, idx2, min);
        
        for (t_index j=1; j<N-1; ++j) {
            prev_node = idx2;
            active_nodes.remove(prev_node);
            
            idx2 = active_nodes.succ[0];
            min = d[idx2];
            
            for (i=idx2; i<N; i=active_nodes.succ[i]) {
                t_float tmp = dist(i, prev_node);
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
                if (d[i] > tmp)
                    d[i] = tmp;
                else if (fc_isnan(tmp))
                    throw (nan_error());
#if HAVE_DIAGNOSTIC
#pragma GCC diagnostic pop
#endif
                if (d[i] < min) {
                    min = d[i];
                    idx2 = i;
                }
            }
            Z2.append(prev_node, idx2, min);
        }
    }
    
    
    template <const unsigned char method, typename t_dissimilarity>
    void generic_linkage_vector(const t_index N,
                                t_dissimilarity & dist,
                                cluster_result & Z2)
    {
        /*
         N: integer, number of data points
         dist: function pointer to the metric
         Z2: output data structure
         
         This algorithm is valid for the distance update methods
         "Ward", "centroid" and "median" only!
         */
        const t_index N_1 = N-1;
        t_index i, j; // loop variables
        t_index idx1, idx2; // row and column indices
        
        auto_array_ptr<t_index> n_nghbr(N_1); // array of nearest neighbors
        auto_array_ptr<t_float> mindist(N_1); // distances to the nearest neighbors
        auto_array_ptr<t_index> row_repr(N); // row_repr[i]: node number that the
        // i-th row represents
        doubly_linked_list active_nodes(N);
        binary_min_heap nn_distances(&*mindist, N_1); // minimum heap structure for
        // the distance to the nearest neighbor of each point
        t_index node1, node2;     // node numbers in the output
        t_float min; // minimum and row index for nearest-neighbor search
        
        for (i=0; i<N; ++i)
            // Build a list of row ↔ node label assignments.
            // Initially i ↦ i
            row_repr[i] = i;
        
        // Initialize the minimal distances:
        // Find the nearest neighbor of each point.
        // n_nghbr[i] = argmin_{j>i} D(i,j) for i in range(N-1)
        for (i=0; i<N_1; ++i) {
            min = std::numeric_limits<t_float>::infinity();
            t_index idx;
            for (idx=j=i+1; j<N; ++j) {
                t_float tmp;
                switch (method) {
                    case METHOD_METR_WARD:
                        tmp = dist.ward_initial(i,j);
                        break;
                    default:
                        tmp = dist.template sqeuclidean<true>(i,j);
                }
                if (tmp<min) {
                    min = tmp;
                    idx = j;
                }
            }
            switch (method) {
                case METHOD_METR_WARD:
                    mindist[i] = t_dissimilarity::ward_initial_conversion(min);
                    break;
                default:
                    mindist[i] = min;
            }
            n_nghbr[i] = idx;
        }
        
        // Put the minimal distances into a heap structure to make the repeated
        // global minimum searches fast.
        nn_distances.heapify();
        
        // Main loop: We have N-1 merging steps.
        for (i=0; i<N_1; ++i) {
            idx1 = nn_distances.argmin();
            
            while ( active_nodes.is_inactive(n_nghbr[idx1]) ) {
                // Recompute the minimum mindist[idx1] and n_nghbr[idx1].
                n_nghbr[idx1] = j = active_nodes.succ[idx1]; // exists, maximally N-1
                switch (method) {
                    case METHOD_METR_WARD:
                        min = dist.ward(idx1,j);
                        for (j=active_nodes.succ[j]; j<N; j=active_nodes.succ[j]) {
                            t_float const tmp = dist.ward(idx1,j);
                            if (tmp<min) {
                                min = tmp;
                                n_nghbr[idx1] = j;
                            }
                        }
                        break;
                    default:
                        min = dist.template sqeuclidean<true>(idx1,j);
                        for (j=active_nodes.succ[j]; j<N; j=active_nodes.succ[j]) {
                            t_float const tmp = dist.template sqeuclidean<true>(idx1,j);
                            if (tmp<min) {
                                min = tmp;
                                n_nghbr[idx1] = j;
                            }
                        }
                }
                /* Update the heap with the new true minimum and search for the (possibly
                 different) minimal entry. */
                nn_distances.update_geq(idx1, min);
                idx1 = nn_distances.argmin();
            }
            
            nn_distances.heap_pop(); // Remove the current minimum from the heap.
            idx2 = n_nghbr[idx1];
            
            // Write the newly found minimal pair of nodes to the output array.
            node1 = row_repr[idx1];
            node2 = row_repr[idx2];
            
            Z2.append(node1, node2, mindist[idx1]);
            
            switch (method) {
                case METHOD_METR_WARD:
                case METHOD_METR_CENTROID:
                    dist.merge_inplace(idx1, idx2);
                    break;
                case METHOD_METR_MEDIAN:
                    dist.merge_inplace_weighted(idx1, idx2);
                    break;
                default:
                    throw std::runtime_error(std::string("Invalid method."));
            }
            
            // Index idx2 now represents the new (merged) node with label N+i.
            row_repr[idx2] = N+i;
            // Remove idx1 from the list of active indices (active_nodes).
            active_nodes.remove(idx1);  // TBD later!!!
            
            // Update the distance matrix
            switch (method) {
                case METHOD_METR_WARD:
                    /*
                     Ward linkage.
                     
                     Shorter and longer distances can occur, not smaller than min(d1,d2)
                     but maybe bigger than max(d1,d2).
                     */
                    // Update the distance matrix in the range [start, idx1).
                    for (j=active_nodes.start; j<idx1; j=active_nodes.succ[j]) {
                        if (n_nghbr[j] == idx2) {
                            n_nghbr[j] = idx1; // invalidate
                        }
                    }
                    // Update the distance matrix in the range (idx1, idx2).
                    for ( ; j<idx2; j=active_nodes.succ[j]) {
                        t_float const tmp = dist.ward(j, idx2);
                        if (tmp < mindist[j]) {
                            nn_distances.update_leq(j, tmp);
                            n_nghbr[j] = idx2;
                        }
                        else if (n_nghbr[j]==idx2) {
                            n_nghbr[j] = idx1; // invalidate
                        }
                    }
                    // Find the nearest neighbor for idx2.
                    if (idx2<N_1) {
                        n_nghbr[idx2] = j = active_nodes.succ[idx2]; // exists, maximally N-1
                        min = dist.ward(idx2,j);
                        for (j=active_nodes.succ[j]; j<N; j=active_nodes.succ[j]) {
                            t_float const tmp = dist.ward(idx2,j);
                            if (tmp < min) {
                                min = tmp;
                                n_nghbr[idx2] = j;
                            }
                        }
                        nn_distances.update(idx2, min);
                    }
                    break;
                    
                default:
                    /*
                     Centroid and median linkage.
                     
                     Shorter and longer distances can occur, not bigger than max(d1,d2)
                     but maybe smaller than min(d1,d2).
                     */
                    for (j=active_nodes.start; j<idx2; j=active_nodes.succ[j]) {
                        t_float const tmp = dist.template sqeuclidean<true>(j, idx2);
                        if (tmp < mindist[j]) {
                            nn_distances.update_leq(j, tmp);
                            n_nghbr[j] = idx2;
                        }
                        else if (n_nghbr[j] == idx2)
                            n_nghbr[j] = idx1; // invalidate
                    }
                    // Find the nearest neighbor for idx2.
                    if (idx2<N_1) {
                        n_nghbr[idx2] = j = active_nodes.succ[idx2]; // exists, maximally N-1
                        min = dist.template sqeuclidean<true>(idx2,j);
                        for (j=active_nodes.succ[j]; j<N; j=active_nodes.succ[j]) {
                            t_float const tmp = dist.template sqeuclidean<true>(idx2, j);
                            if (tmp < min) {
                                min = tmp;
                                n_nghbr[idx2] = j;
                            }
                        }
                        nn_distances.update(idx2, min);
                    }
            }
        }
    }
    
    
    template <const unsigned char method, typename t_dissimilarity>
    void generic_linkage_vector_alternative(const t_index N,
                                            t_dissimilarity & dist,
                                            cluster_result & Z2)
    {
        /*
         N: integer, number of data points
         dist: function pointer to the metric
         Z2: output data structure
         
         This algorithm is valid for the distance update methods
         "Ward", "centroid" and "median" only!
         */
        const t_index N_1 = N-1;
        t_index i, j=0; // loop variables
        t_index idx1, idx2; // row and column indices
        
        auto_array_ptr<t_index> n_nghbr(2*N-2); // array of nearest neighbors
        auto_array_ptr<t_float> mindist(2*N-2); // distances to the nearest neighbors
        
        doubly_linked_list active_nodes(N+N_1);
        binary_min_heap nn_distances(&*mindist, N_1, 2*N-2, 1); // minimum heap
        // structure for the distance to the nearest neighbor of each point
        
        t_float min; // minimum for nearest-neighbor searches
        
        // Initialize the minimal distances:
        // Find the nearest neighbor of each point.
        // n_nghbr[i] = argmin_{j>i} D(i,j) for i in range(N-1)
        for (i=1; i<N; ++i) {
            min = std::numeric_limits<t_float>::infinity();
            t_index idx;
            for (idx=j=0; j<i; ++j) {
                t_float tmp;
                switch (method) {
                    case METHOD_METR_WARD:
                        tmp = dist.ward_initial(i,j);
                        break;
                    default:
                        tmp = dist.template sqeuclidean<true>(i,j);
                }
                if (tmp<min) {
                    min = tmp;
                    idx = j;
                }
            }
            switch (method) {
                case METHOD_METR_WARD:
                    mindist[i] = t_dissimilarity::ward_initial_conversion(min);
                    break;
                default:
                    mindist[i] = min;
            }
            n_nghbr[i] = idx;
        }
        
        // Put the minimal distances into a heap structure to make the repeated
        // global minimum searches fast.
        nn_distances.heapify();
        
        // Main loop: We have N-1 merging steps.
        for (i=N; i<N+N_1; ++i) {
            /*
             The bookkeeping is different from the "stored matrix approach" algorithm
             generic_linkage.
             
             mindist[i] stores a lower bound on the minimum distance of the point i to
             all points of *lower* index:
             
             mindist[i] ≥ min_{j<i} D(i,j)
             
             Moreover, new nodes do not re-use one of the old indices, but they are
             given a new, unique index (SciPy convention: initial nodes are 0,…,N−1,
             new nodes are N,…,2N−2).
             
             Invalid nearest neighbors are not recognized by the fact that the stored
             distance is smaller than the actual distance, but the list active_nodes
             maintains a flag whether a node is inactive. If n_nghbr[i] points to an
             active node, the entries nn_distances[i] and n_nghbr[i] are valid,
             otherwise they must be recomputed.
             */
            idx1 = nn_distances.argmin();
            while ( active_nodes.is_inactive(n_nghbr[idx1]) ) {
                // Recompute the minimum mindist[idx1] and n_nghbr[idx1].
                n_nghbr[idx1] = j = active_nodes.start;
                switch (method) {
                    case METHOD_METR_WARD:
                        min = dist.ward_extended(idx1,j);
                        for (j=active_nodes.succ[j]; j<idx1; j=active_nodes.succ[j]) {
                            t_float tmp = dist.ward_extended(idx1,j);
                            if (tmp<min) {
                                min = tmp;
                                n_nghbr[idx1] = j;
                            }
                        }
                        break;
                    default:
                        min = dist.sqeuclidean_extended(idx1,j);
                        for (j=active_nodes.succ[j]; j<idx1; j=active_nodes.succ[j]) {
                            t_float const tmp = dist.sqeuclidean_extended(idx1,j);
                            if (tmp<min) {
                                min = tmp;
                                n_nghbr[idx1] = j;
                            }
                        }
                }
                /* Update the heap with the new true minimum and search for the (possibly
                 different) minimal entry. */
                nn_distances.update_geq(idx1, min);
                idx1 = nn_distances.argmin();
            }
            
            idx2 = n_nghbr[idx1];
            active_nodes.remove(idx1);
            active_nodes.remove(idx2);
            
            Z2.append(idx1, idx2, mindist[idx1]);
            
            if (i<2*N_1) {
                switch (method) {
                    case METHOD_METR_WARD:
                    case METHOD_METR_CENTROID:
                        dist.merge(idx1, idx2, i);
                        break;
                        
                    case METHOD_METR_MEDIAN:
                        dist.merge_weighted(idx1, idx2, i);
                        break;
                        
                    default:
                        throw std::runtime_error(std::string("Invalid method."));
                }
                
                n_nghbr[i] = active_nodes.start;
                if (method==METHOD_METR_WARD) {
                    /*
                     Ward linkage.
                     
                     Shorter and longer distances can occur, not smaller than min(d1,d2)
                     but maybe bigger than max(d1,d2).
                     */
                    min = dist.ward_extended(active_nodes.start, i);
                    for (j=active_nodes.succ[active_nodes.start]; j<i;
                         j=active_nodes.succ[j]) {
                        t_float tmp = dist.ward_extended(j, i);
                        if (tmp < min) {
                            min = tmp;
                            n_nghbr[i] = j;
                        }
                    }
                }
                else {
                    /*
                     Centroid and median linkage.
                     
                     Shorter and longer distances can occur, not bigger than max(d1,d2)
                     but maybe smaller than min(d1,d2).
                     */
                    min = dist.sqeuclidean_extended(active_nodes.start, i);
                    for (j=active_nodes.succ[active_nodes.start]; j<i;
                         j=active_nodes.succ[j]) {
                        t_float tmp = dist.sqeuclidean_extended(j, i);
                        if (tmp < min) {
                            min = tmp;
                            n_nghbr[i] = j;
                        }
                    }
                }
                if (idx2<active_nodes.start)  {
                    nn_distances.remove(active_nodes.start);
                } else {
                    nn_distances.remove(idx2);
                }
                nn_distances.replace(idx1, i, min);
            }
        }
    }
}

#endif
