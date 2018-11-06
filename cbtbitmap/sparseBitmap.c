/* ***************************************************************************
 * Copyright 2018 VMware, Inc.  All rights reserved.
 * -- VMware Confidential
 * **************************************************************************/

/*
 * CBT Bitmap Sparse Bitmap implementation file
 *
 * It applies a Trie forest as a sparse bitmap to implement all public APIs.
 *
 * Design doc:
 * https://confluence.eng.vmware.com/display/VDDK/CBT+Block+Size#CBTBlockSize-SparseBitmap
 *
 */

#include "cbtBitmap.h"

#ifndef VMKERNEL
#include <stdlib.h>
#include <string.h>
#include <assert.h>
   #ifndef ASSERT
      #define ASSERT(x) assert(x)
   #endif
   #ifdef _MSC_VER
      #define inline
   #endif
#else
#include "vm_types.h"
#include "vm_libc.h"
#include "vm_assert.h"
#include "vmkernel.h"
#include "libc.h"
#endif


// definition of macros
#define MAX_NUM_TRIES 6
#define ADDR_BITS_IN_LEAF 9
#define ADDR_BITS_IN_INNER_NODE 3
#define ADDR_BITS_IN_HEIGHT(h) \
   (ADDR_BITS_IN_LEAF+((h)-1)*ADDR_BITS_IN_INNER_NODE)
#define NUM_TRIE_WAYS (1u << ADDR_BITS_IN_INNER_NODE)
#define TRIE_WAY_MASK ((uint64)NUM_TRIE_WAYS-1)
#define MAX_NUM_LEAVES (1u << ((MAX_NUM_TRIES-1)*ADDR_BITS_IN_INNER_NODE))

#define COUNT_SET_BITS(x, c) \
   while((x) != 0) {         \
      ++(c);                 \
      (x) &= (x)-1;          \
   }

#define GET_BITMAP_BYTE_BIT(offset, byte, bit) \
   do {                                        \
      byte = offset >> 3;                      \
      bit = offset & 0x7;                      \
   } while (0)

#define GET_BITMAP_BYTE8_BIT(offset, byte, bit) \
   do {                                         \
      byte = offset >> 6;                       \
      bit = offset & 0x3f;                      \
   } while (0)


typedef union TrieNode {
   union TrieNode *_children[NUM_TRIE_WAYS];
   char _bitmap[NUM_TRIE_WAYS * sizeof(union TrieNode*)];
} *TrieNode;

#define LEAF_VALUE_MASK ((uint64)sizeof(union TrieNode) * 8 - 1)
#define NODE_ADDR_MASK(h)        \
   (((h)==0) ? ~LEAF_VALUE_MASK :  \
      ~(((uint64)1ull << ADDR_BITS_IN_HEIGHT(h)) - 1))
#define NODE_VALUE_MASK(h)  (~NODE_ADDR_MASK(h))
#define NODE_MAX_ADDR(addr, h) ((addr) | NODE_VALUE_MASK((h)+1))

#define GET_NODE_WAYS(addr, h) \
   (((addr)>>ADDR_BITS_IN_HEIGHT(h)) & TRIE_WAY_MASK)

#define TRIE_STAT_FLAG_BITSET                1
#define TRIE_STAT_FLAG_MEMORY_ALLOC          (1 << 1)
#define TRIE_STAT_FLAG_COUNT_STREAM_ITEM     (1 << 2)
#define TRIE_STAT_FLAG_OOM_AS_COLLAPSED      (1 << 3)

#define TRIE_STAT_FLAG_IS_NULL(f) ((f) == 0)
#define IS_TRIE_STAT_FLAG_BITSET_ON(f) ((f) & TRIE_STAT_FLAG_BITSET)
#define IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(f) ((f) & TRIE_STAT_FLAG_MEMORY_ALLOC)
#define IS_TRIE_STAT_FLAG_COUNT_STREAM_ITEM_ON(f) \
   ((f) & TRIE_STAT_FLAG_COUNT_STREAM_ITEM)
#define IS_TRIE_STAT_FLAG_OOM_AS_COLLAPSED_ON(f) \
   ((f) & TRIE_STAT_FLAG_OOM_AS_COLLAPSED)

#define TRIE_COLLAPSED_NODE_ADDR ((TrieNode)-1)

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

// definition of data structures
typedef struct TrieStatistics {
   uint32 _totalSet;
   uint32 _memoryInUse;
   uint16 _streamItemCount;
   uint16 _flag;
} TrieStatistics;

struct CBTBitmap {
   TrieNode _tries[MAX_NUM_TRIES];
   TrieStatistics _stat;
};

typedef struct BlockTrackingBitmapCallbackData {
   void *_cb;
   void *_cbData;
} BlockTrackingBitmapCallbackData;


// pack it to avoid waste bytes with allignment for serialization stream
typedef
#ifndef CBT_BITMAP_UNITTEST
#include "vmware_pack_begin.h"
#endif
struct BlockTrackingSparseBitmapStream {
   uint16 _nodeOffset;
   union {
      union TrieNode _node;
      struct {
         uint64 _addr;
         uint8 _height;
      } _collapsedNode;
   } _payLoad;
}
#ifndef CBT_BITMAP_UNITTEST
#include "vmware_pack_end.h"
#else
__attribute__((__packed__))
#endif
BlockTrackingSparseBitmapStream;

#define STREAM_ITEM_END ((uint16)-1)
#define STREAM_ITEM_COLLAPSED_NODE ((uint16)-2)

// visitor pattern
typedef enum {
   TRIE_VISITOR_RET_CONT = 0,
   TRIE_VISITOR_RET_END,
   TRIE_VISITOR_RET_SKIP_CHILDREN,
   TRIE_VISITOR_RET_OUT_OF_MEM,
   TRIE_VISITOR_RET_OVERFLOW,
   TRIE_VISITOR_RET_ABORT,
} TrieVisitorReturnCode;

struct BlockTrackingSparseBitmapVisitor;
typedef TrieVisitorReturnCode (*VisitInnerNode) (
      struct BlockTrackingSparseBitmapVisitor *visitor,
      uint64 nodeAddr, uint8 height, uint64 fromAddr, uint64 toAddr,
      TrieNode *pNode);

typedef TrieVisitorReturnCode (*VisitLeafNode) (
      struct BlockTrackingSparseBitmapVisitor *visitor,
      uint64 nodeAddr, uint16 fromOffset, uint16 toOffset, TrieNode *pNode);

typedef struct BlockTrackingSparseBitmapVisitor {
   VisitLeafNode _visitLeafNode;
   VisitInnerNode _beforeVisitInnerNode;
   VisitInnerNode _afterVisitInnerNode;
   VisitInnerNode _visitNullNode;
   VisitInnerNode _visitCollapsedNode;
   void *_data;
   TrieStatistics *_stat;
} BlockTrackingSparseBitmapVisitor;


// globals
static Bool g_IsInited;
static CBTBitmapAllocator g_Allocator;


// forward declaration

static inline TrieVisitorReturnCode
DeleteInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                uint64 nodeAddr, uint8 height,
                uint64 fromAddr, uint64 toAddr,
                TrieNode *pNode);

static inline TrieVisitorReturnCode
DeleteLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
               uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
               TrieNode *pNode);

static inline TrieVisitorReturnCode
DeleteCollapsedNode(BlockTrackingSparseBitmapVisitor *visitor,
                    uint64 nodeAddr, uint8 height,
                    uint64 fromAddr, uint64 toAddr,
                    TrieNode *pNode);

static inline uint16
TrieGetSetBitsInLeaf(TrieNode leaf, uint16 fromOffset, uint16 toOffset);

static TrieVisitorReturnCode
TrieAccept(TrieNode *pNode, uint64 nodeAddr, uint8 height,
           uint64 fromAddr, uint64 toAddr,
           BlockTrackingSparseBitmapVisitor *visitor);

////////////////////////////////////////////////////////////////////////////////
//   Allocate/Free Functions
////////////////////////////////////////////////////////////////////////////////


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmapDefaultAlloc --
 *
 *    The default allocator
 *
 * Parameter:
 *    data - input. Customer data
 *    size - input. The requested allocation size.
 *
 * Results:
 *    The pointer to the allocated chunk or NULL if error.
 *
 *-----------------------------------------------------------------------------
 */

static void *
CBTBitmapDefaultAlloc(void *data, uint64 size)
{
#ifndef VMKERNEL
   return malloc(size);
#else
   ASSERT(0);
   return NULL;
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmapDefaultFree --
 *
 *    The default de-allocator
 *
 * Parameter:
 *    data - input. Customer data
 *    ptr  - input. The pointer to the allocated memory.
 *
 *-----------------------------------------------------------------------------
 */
static void
CBTBitmapDefaultFree(void *data, void *ptr)
{
#ifndef VMKERNEL
   free(ptr);
#else
   ASSERT(0);
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * AllocateTrieNode --
 *
 *    Allocate a trie node.
 *
 * Parameter:
 *    stat - input. A pointer to statistics object.
 *    isLeaf - input. Indicate to allocate a leaf node.
 *
 * Results:
 *    The trie node or NULL if error.
 *
 *-----------------------------------------------------------------------------
 */

static inline TrieNode
AllocateTrieNode(TrieStatistics *stat, Bool isLeaf)
{
   TrieNode node =
          (TrieNode)g_Allocator.allocate(g_Allocator._data, sizeof(*node));
   if (node != NULL) {
      memset(node, 0, sizeof *node);
      if (stat != NULL) {
         if (IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(stat->_flag)) {
            stat->_memoryInUse += sizeof(*node);
         }
         if (isLeaf && IS_TRIE_STAT_FLAG_COUNT_STREAM_ITEM_ON(stat->_flag)) {
            stat->_streamItemCount++;
         }
      }
   }
   return node;
}


/*
 *-----------------------------------------------------------------------------
 *
 * FreeTrieNode --
 *
 *    Free a trie node.
 *
 * Parameter:
 *    TrieNode - input. The node instance.
 *    stat - input. A pointer to statistics object.
 *
 *-----------------------------------------------------------------------------
 */

static inline void
FreeTrieNode(TrieNode node, Bool isLeaf, TrieStatistics *stat)
{
   if (stat != NULL) {
      if (IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(stat->_flag)) {
         stat->_memoryInUse -= sizeof(*node);
      }
      if (isLeaf) {
         if (IS_TRIE_STAT_FLAG_COUNT_STREAM_ITEM_ON(stat->_flag)) {
            stat->_streamItemCount--;
         }
         if (IS_TRIE_STAT_FLAG_BITSET_ON(stat->_flag)) {
            stat->_totalSet -= TrieGetSetBitsInLeaf(node, 0, LEAF_VALUE_MASK);
         }
      }
   }
   g_Allocator.deallocate(g_Allocator._data, node);
}


/*
 *-----------------------------------------------------------------------------
 *
 * AllocateBitmap --
 *
 *    Allocate the memory of a CBT bitmap
 *
 * Results:
 *    An allocated CBT bitmap without initialization.
 *
 *-----------------------------------------------------------------------------
 */

static inline CBTBitmap
AllocateBitmap()
{
#ifdef VMKERNEL
   ASSERT_ON_COMPILE(sizeof(struct CBTBitmap) <= sizeof(union TrieNode));
   ASSERT_ON_COMPILE(sizeof(union TrieNode) == CACHELINE_SIZE);
#endif
   CBTBitmap bitmap =
          (CBTBitmap)g_Allocator.allocate(g_Allocator._data, sizeof(*bitmap));
   if (bitmap != NULL) {
      memset(bitmap, 0, sizeof *bitmap);
   }
   return bitmap;
}


/*
 *-----------------------------------------------------------------------------
 *
 * FreeBitmap --
 *
 *    Free the memory of a CBT bitmap
 *
 * Parameter:
 *    bitmap - input. A CBT bitmap.
 *
 *-----------------------------------------------------------------------------
 */

static inline void
FreeBitmap(CBTBitmap bitmap)
{
   g_Allocator.deallocate(g_Allocator._data, bitmap);
}


////////////////////////////////////////////////////////////////////////////////
//   Trie Functions
////////////////////////////////////////////////////////////////////////////////


/*
 *-----------------------------------------------------------------------------
 *
 * TrieIsCollapsedNode --
 *
 *    The helper function to identify a collapsed node.
 *
 * Parameter:
 *    node - input. The node for the check.
 *
 * Results:
 *    Return true of the node is a collapsed node, otherwise return false.
 *
 *-----------------------------------------------------------------------------
 */

static inline Bool
TrieIsCollapsedNode(TrieNode node)
{
   return node == TRIE_COLLAPSED_NODE_ADDR;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TrieCollapseNode --
 *
 *    The helper function to collapse the node.
 *
 * Parameter:
 *    pNode - input/output. A pointer to the node to be collapsed.
 *    nodeAddr - input. The node address.
 *    height - input. The trie height of the node.
 *    state - input. A pointer to the statistics object
 *
 *-----------------------------------------------------------------------------
 */

static inline void
TrieCollapseNode(TrieNode *pNode, uint64 nodeAddr, uint8 height,
                 TrieStatistics *stat)
{
   TrieNode node;
   ASSERT(pNode != NULL);
   node = *pNode;
   if (TrieIsCollapsedNode(node)) {
      return;
   }
   if (node != NULL) {
      FreeTrieNode(*pNode, height == 0, stat);
   }
   *pNode = TRIE_COLLAPSED_NODE_ADDR;
   if (stat != NULL) {
      if (IS_TRIE_STAT_FLAG_COUNT_STREAM_ITEM_ON(stat->_flag)) {
         stat->_streamItemCount++;
         if (node != NULL && height > 0) {
            stat->_streamItemCount -= NUM_TRIE_WAYS;
         }
      }
      if (IS_TRIE_STAT_FLAG_BITSET_ON(stat->_flag) &&
          (node == NULL || height == 0)) {
         stat->_totalSet += NODE_MAX_ADDR(nodeAddr, height) - nodeAddr + 1;
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TrieIsFullNode --
 *
 *    The helper function to identify a full node.
 *
 *    Full node means all bits under that node are set.
 *
 * Parameter:
 *    node - input. The node for the check.
 *
 * Results:
 *    Return true of the node is a full node, otherwise return false.
 *
 *-----------------------------------------------------------------------------
 */

static inline Bool
TrieIsFullNode(TrieNode node)
{
   uint8 i;
   for (i = 0 ; i < NUM_TRIE_WAYS ; ++i) {
      if (!TrieIsCollapsedNode(node->_children[i])) {
         return FALSE;
      }
   }
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TrieGetSetBitsInLeaf --
 *
 *    The helper function to get the count of set-bits in a leaf node.
 *
 * Parameter:
 *    leaf - input. A leaf node.
 *    fromOffset - input. Offset to count the set-bits in the leaf.
 *    toOffset - input. The end of offset to count the set-bits in the leaf.
 *
 * Results:
 *    The count of set-bits.
 *
 *-----------------------------------------------------------------------------
 */

static inline uint16
TrieGetSetBitsInLeaf(TrieNode leaf, uint16 fromOffset, uint16 toOffset)
{
   uint8 i;
   uint8 byte, bit;
   uint8 toByte, toBit;
   uint64 c;
   uint16 cnt = 0;
   uint64 *bitmap = (uint64 *)leaf->_bitmap;

   GET_BITMAP_BYTE8_BIT(fromOffset, byte, bit);
   GET_BITMAP_BYTE8_BIT(toOffset, toByte, toBit);

   c = bitmap[byte];
   c &= ~((1ull << bit) - 1);
   if (byte != toByte) {
      COUNT_SET_BITS(c, cnt);

      for (i = byte + 1 ; i < toByte ; ++i) {
         c = bitmap[i];
         COUNT_SET_BITS(c, cnt);
      }
      c = bitmap[toByte];
   }
   c &= ~((~((1ull << toBit) - 1)) << 1);
   COUNT_SET_BITS(c, cnt);
   return cnt;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TrieLeafNodeMergeFlatBitmap --
 *
 *    The helper function to merge the flat bitmap to that of a leaf node.
 *
 * Parameter:
 *    destNode - input/output. The leaf node which is merging to.
 *    flatBitmap - input. The flat bitmap which is merging from.
 *    stat - input. The statistics instance.
 *
 * Results:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

static inline void
TrieLeafNodeMergeFlatBitmap(TrieNode destNode, const char *flatBitmap,
                            TrieStatistics *stat)
{
   uint8 i;
   uint64 *destBitmap = (uint64*)destNode->_bitmap;
   const uint64 *srcBitmap = (const uint64*)flatBitmap;
   if (stat != NULL && IS_TRIE_STAT_FLAG_BITSET_ON(stat->_flag)) {
      for (i = 0 ; i < sizeof(*destNode)/sizeof(uint64) ; ++i) {
         uint64 o = destBitmap[i];
         uint64 n = o | srcBitmap[i];
         o ^= n;
         COUNT_SET_BITS(o, stat->_totalSet);
         destBitmap[i] = n;
      }
   } else {
      for (i = 0 ; i < sizeof(*destNode)/sizeof(uint64) ; ++i) {
         destBitmap[i] |= srcBitmap[i];
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * TrieAccept --
 *
 *    Accept a trie visitor.
 *
 * Parameter:
 *    pNode- input/output. A pointer to a leaf node.
 *    height - input. The height of the node in the tree.
 *    fromAddr - input/output. A pointer to the start address in the scope
 *    toAddre - input. The end address in the scope
 *    visitor - input. A pointer to trie visitor instance.
 *
 * Results:
 *    The error code.
 *
 *-----------------------------------------------------------------------------
 */

static TrieVisitorReturnCode
TrieAccept(TrieNode *pNode, uint64 nodeAddr, uint8 height,
           uint64 fromAddr, uint64 toAddr,
           BlockTrackingSparseBitmapVisitor *visitor)
{
   TrieVisitorReturnCode ret = TRIE_VISITOR_RET_CONT;
   ASSERT(fromAddr <= toAddr);
   ASSERT(nodeAddr <= toAddr);

   if (*pNode == NULL) {
      if (visitor->_visitNullNode == NULL) {
         // ignore NULL node and continue the traverse
         goto exit;
      }
      if ((ret = visitor->_visitNullNode(visitor, nodeAddr, height,
                                         fromAddr, toAddr, pNode))
            != TRIE_VISITOR_RET_CONT) {
         goto exit;
      }
   }
   if (TrieIsCollapsedNode(*pNode)) {
      // collapsed node
      if (visitor->_visitCollapsedNode != NULL) {
         if ((ret = visitor->_visitCollapsedNode(visitor, nodeAddr, height,
                                                 fromAddr, toAddr, pNode))
               != TRIE_VISITOR_RET_CONT) {
            goto exit;
         }
      }
   } else if (height == 0) {
      // leaf
      uint16 fromOffset = MAX(nodeAddr, fromAddr) & LEAF_VALUE_MASK;
      uint16 toOffset =
         (toAddr > NODE_MAX_ADDR(nodeAddr, height)) ?
                LEAF_VALUE_MASK : toAddr & LEAF_VALUE_MASK;
      if (visitor->_visitLeafNode != NULL) {
         if ((ret = visitor->_visitLeafNode(visitor, nodeAddr,
                                            fromOffset, toOffset, pNode))
               != TRIE_VISITOR_RET_CONT) {
            goto exit;
         }
      }
   } else {
      // inner node
      uint8 way;
      uint64 chldNodeAddr;
      if (visitor->_beforeVisitInnerNode != NULL) {
         if ((ret = visitor->_beforeVisitInnerNode(visitor, nodeAddr, height,
                                                   fromAddr, toAddr, pNode))
               != TRIE_VISITOR_RET_CONT) {
            goto exit;
         }
      }
      for (way = GET_NODE_WAYS(MAX(fromAddr, nodeAddr), height),
           chldNodeAddr =
              (nodeAddr & ~(TRIE_WAY_MASK << ADDR_BITS_IN_HEIGHT(height))) |
              (way << ADDR_BITS_IN_HEIGHT(height));
           way < NUM_TRIE_WAYS && chldNodeAddr <= toAddr;
           ++way, chldNodeAddr += NODE_VALUE_MASK(height) + 1) {
         ret = TrieAccept(&(*pNode)->_children[way], chldNodeAddr, height-1,
                          fromAddr, toAddr, visitor);
         if (ret != TRIE_VISITOR_RET_CONT &&
             ret != TRIE_VISITOR_RET_SKIP_CHILDREN) {
            goto exit;
         }
      }
      if (visitor->_afterVisitInnerNode != NULL) {
         if ((ret = visitor->_afterVisitInnerNode(visitor, nodeAddr, height,
                                                  fromAddr, toAddr, pNode))
               != TRIE_VISITOR_RET_CONT) {
            goto exit;
         }
      }
   }
exit:
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TrieMerge --
 *
 *    Merge two trie nodes recursively.
 *
 * Parameter:
 *    pDestNode- input/output. A pointer to a leaf node which is merging to.
 *    srcMode- input. A leaf node which is merging from.
 *    stat - input. The statistics instance.
 *
 * Results:
 *    The error code.
 *
 *-----------------------------------------------------------------------------
 */

static TrieVisitorReturnCode
TrieMerge(TrieNode *pDestNode, TrieNode srcNode,
          uint64 nodeAddr, uint8 height,
          TrieStatistics *stat)
{
   TrieVisitorReturnCode ret = TRIE_VISITOR_RET_CONT;
   if (srcNode == NULL || TrieIsCollapsedNode(*pDestNode)) {
      // nothing to merge from src
      goto exit;
   }

   if (TrieIsCollapsedNode(srcNode)) {
      // free the trie
      BlockTrackingSparseBitmapVisitor deleteTrie = {
         DeleteLeafNode,
         NULL,
         DeleteInnerNode,
         NULL,
         DeleteCollapsedNode,
         NULL,
         stat
      };
      TrieAccept(pDestNode, nodeAddr,  height, 0, -1, &deleteTrie);
      *pDestNode = NULL;
      // collapse the dest node
      TrieCollapseNode(pDestNode, nodeAddr, height, stat);
      goto exit;
   }

   if (*pDestNode == NULL) {
      *pDestNode = AllocateTrieNode(stat, height == 0);
      if (*pDestNode == NULL) {
         if (stat != NULL &&
             IS_TRIE_STAT_FLAG_OOM_AS_COLLAPSED_ON(stat->_flag)) {
            TrieCollapseNode(pDestNode, nodeAddr, height, stat);
         } else {
            ret = TRIE_VISITOR_RET_OUT_OF_MEM;
         }
         goto exit;
      }
   }

   if (height == 0) {
      // leaf
      TrieLeafNodeMergeFlatBitmap(*pDestNode, srcNode->_bitmap, stat);
   } else {
      // inner node
      uint8 way;
      uint64 chldNodeAddr;
      for (way = 0, chldNodeAddr = nodeAddr;
           way < NUM_TRIE_WAYS;
           ++way, chldNodeAddr += NODE_VALUE_MASK(height) + 1) {
         ret = TrieMerge(&(*pDestNode)->_children[way], srcNode->_children[way],
                         chldNodeAddr, height-1, stat);
         if (ret != TRIE_VISITOR_RET_CONT &&
             ret != TRIE_VISITOR_RET_SKIP_CHILDREN) {
            goto exit;
         }
      }
   }
   if (TrieIsFullNode(*pDestNode)) {
      TrieCollapseNode(pDestNode, nodeAddr, height, stat);
   }
exit:
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TrieMaxHeight  --
 *
 *    Calulate the maximum height of the trie by the address.
 *
 * Parameter:
 *    addr - input. The address.
 *
 * Results:
 *    the height.
 *
 *-----------------------------------------------------------------------------
 */

static inline uint8
TrieMaxHeight(uint64 addr)
{
   uint8 height = 0;
   addr >>= ADDR_BITS_IN_LEAF;
   while (addr != 0) {
      addr >>= ADDR_BITS_IN_INNER_NODE;
      ++height;
   }
   return height;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TrieIndexValidation --
 *
 *    Check if the trie index is in range or not.
 *
 * Parameter:
 *    trieIndx - input. The trie index.
 *
 * Results:
 *    Return TRUE if the index is in range.
 *
 *-----------------------------------------------------------------------------
 */

static inline Bool
TrieIndexValidation(uint8 trieIndex)
{
   return trieIndex < MAX_NUM_TRIES;
}


////////////////////////////////////////////////////////////////////////////////
//   Visitors
////////////////////////////////////////////////////////////////////////////////


/**
 *  A vistor to delete trie
 */

static inline TrieVisitorReturnCode
DeleteInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                uint64 nodeAddr, uint8 height,
                uint64 fromAddr, uint64 toAddr,
                TrieNode *pNode)
{
   FreeTrieNode(*pNode, height == 0, visitor->_stat);
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
DeleteLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
               uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
               TrieNode *pNode)
{
   FreeTrieNode(*pNode, TRUE, visitor->_stat);
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
DeleteCollapsedNode(BlockTrackingSparseBitmapVisitor *visitor,
                    uint64 nodeAddr, uint8 height,
                    uint64 fromAddr, uint64 toAddr,
                    TrieNode *pNode)
{
   if (visitor->_stat != NULL) {
      if (IS_TRIE_STAT_FLAG_COUNT_STREAM_ITEM_ON(visitor->_stat->_flag)) {
         visitor->_stat->_streamItemCount--;
      }
      if (IS_TRIE_STAT_FLAG_BITSET_ON(visitor->_stat->_flag)) {
         visitor->_stat->_totalSet -=
            NODE_MAX_ADDR(nodeAddr, height) - nodeAddr + 1;
      }
   }
   return TRIE_VISITOR_RET_CONT;
}


/**
 * A visitor to create NULL node
 */

static inline TrieVisitorReturnCode
AllocateNodeVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                          uint64 nodeAddr, uint8 height,
                          uint64 fromAddr, uint64 toAddr,
                          TrieNode *pNode)
{
   *pNode = AllocateTrieNode(visitor->_stat, height == 0);
   if (*pNode == NULL) {
      if (visitor->_stat != NULL &&
          IS_TRIE_STAT_FLAG_OOM_AS_COLLAPSED_ON(visitor->_stat->_flag)) {
         // cannot allocate memory for node breaks the business.
         // In order to continue recording changes without data loss,
         // collapse the node to treate the sub-trie of node is full
         TrieCollapseNode(pNode, nodeAddr, height, visitor->_stat);
      } else {
         return TRIE_VISITOR_RET_OUT_OF_MEM;
      }
   }
   return TRIE_VISITOR_RET_CONT;
}


/**
 * A visitor to query set bit
 */

static inline TrieVisitorReturnCode
QueryBitVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64 nodeAddr, uint8 height,
                      uint64 fromAddr, uint64 toAddr,
                      TrieNode *pNode)
{
   Bool *isSet = (Bool *)visitor->_data;
   *isSet = FALSE;
   return TRIE_VISITOR_RET_END;
}


static inline TrieVisitorReturnCode
QueryBitVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                      TrieNode *pNode)
{
   Bool *isSet = (Bool *)visitor->_data;
   uint8 byte, bit;
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   *isSet = (*pNode)->_bitmap[byte] & (1u << bit);
   return TRIE_VISITOR_RET_END;
}

static inline TrieVisitorReturnCode
QueryBitVisitCollapsedNode(BlockTrackingSparseBitmapVisitor *visitor,
                           uint64 nodeAddr, uint8 height,
                           uint64 fromAddr, uint64 toAddr,
                           TrieNode *pNode)
{
   Bool *isSet = (Bool *)visitor->_data;
   *isSet = TRUE;
   ASSERT(fromAddr == toAddr);
   ASSERT(fromAddr >= nodeAddr && fromAddr <= NODE_MAX_ADDR(nodeAddr, height));
   return TRIE_VISITOR_RET_END;
}


/**
 * A visitor to set bit
 */

static inline TrieVisitorReturnCode
SetBitVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                    uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                    TrieNode *pNode)
{
   Bool *isSet = (Bool *)visitor->_data;
   uint8 byte, bit;
   ASSERT(fromOffset == toOffset);
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   if ((*isSet = (*pNode)->_bitmap[byte] & (1u << bit)) == FALSE) {
      (*pNode)->_bitmap[byte] |= (1u << bit);
      if (visitor->_stat != NULL &&
          IS_TRIE_STAT_FLAG_BITSET_ON(visitor->_stat->_flag)) {
         visitor->_stat->_totalSet++;
      }
   }
   if (TrieIsFullNode(*pNode)) {
      TrieCollapseNode(pNode, nodeAddr, 0, visitor->_stat);
      return TRIE_VISITOR_RET_CONT;
   }
   return TRIE_VISITOR_RET_END;
}

static inline TrieVisitorReturnCode
SetBitPostVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 nodeAddr, uint8 height,
                         uint64 fromAddr, uint64 toAddr,
                         TrieNode *pNode)
{
   // bottom up collapse
   if (TrieIsFullNode(*pNode)) {
      TrieCollapseNode(pNode, nodeAddr, height, visitor->_stat);
      return TRIE_VISITOR_RET_CONT;
   }
   return TRIE_VISITOR_RET_END;
}


/**
 * A visitor to set bits in range
 */

static inline TrieVisitorReturnCode
SetBitsVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                     uint64 nodeAddr, uint8 height,
                     uint64 fromAddr, uint64 toAddr, TrieNode *pNode)
{
   if (nodeAddr >= fromAddr && NODE_MAX_ADDR(nodeAddr, height) <= toAddr) {
      // No need to allocate node since the all addresses in the node should
      // be set. Collapse it directly.
      TrieCollapseNode(pNode, nodeAddr, height, visitor->_stat);
      return TRIE_VISITOR_RET_CONT;
   }
   return AllocateNodeVisitNullNode(visitor, nodeAddr, height,
                                    fromAddr, toAddr, pNode);
}

static inline TrieVisitorReturnCode
SetBitsVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                     uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                     TrieNode *pNode)
{
   uint8 byte, bit;
   uint8 toByte, toBit;
   if (visitor->_stat != NULL &&
       IS_TRIE_STAT_FLAG_BITSET_ON(visitor->_stat->_flag)) {
      visitor->_stat->_totalSet +=
         (toOffset-fromOffset+1) -
         TrieGetSetBitsInLeaf(*pNode, fromOffset, toOffset);
   }
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   if (toOffset == fromOffset) {
      // fast pass for special case
      (*pNode)->_bitmap[byte] |= (1u << bit);
      return TRIE_VISITOR_RET_CONT;
   }
   GET_BITMAP_BYTE_BIT(toOffset, toByte, toBit);
   // fill bits in the middle bytes
   if (toByte - byte > 1) {
      memset(&(*pNode)->_bitmap[byte+1], (int)-1, toByte - byte - 1);
   }
   // fill the bits greater and equal to the bit
   (*pNode)->_bitmap[byte] |= ~((1u<<bit)-1);
   // fill the bits smaller and equal to the max bit
   (*pNode)->_bitmap[toByte] |= ~((~((1u<<toBit)-1)) << 1);

   if (TrieIsFullNode(*pNode)) {
      TrieCollapseNode(pNode, nodeAddr, 0, visitor->_stat);
   }
   return TRIE_VISITOR_RET_CONT;
}


static inline TrieVisitorReturnCode
SetBitsPreVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 nodeAddr, uint8 height,
                         uint64 fromAddr, uint64 toAddr,
                         TrieNode *pNode)
{
   // top down collapse
   if (nodeAddr >= fromAddr && NODE_MAX_ADDR(nodeAddr, height) <= toAddr) {
      // free the trie
      BlockTrackingSparseBitmapVisitor deleteTrie = {
         DeleteLeafNode,
         NULL,
         DeleteInnerNode,
         NULL,
         DeleteCollapsedNode,
         NULL,
         visitor->_stat
      };
      TrieAccept(pNode, nodeAddr, height, fromAddr, toAddr, &deleteTrie);
      *pNode = NULL;
      // collapse itself
      TrieCollapseNode(pNode, nodeAddr, height, visitor->_stat);
      return TRIE_VISITOR_RET_SKIP_CHILDREN;
   }
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
SetBitsPostVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                          uint64 nodeAddr, uint8 height,
                          uint64 fromAddr, uint64 toAddr,
                          TrieNode *pNode)
{
   // bottom up collapse
   if (TrieIsFullNode(*pNode)) {
      TrieCollapseNode(pNode, nodeAddr, height, visitor->_stat);
   }
   return TRIE_VISITOR_RET_CONT;
}


/**
 * A visitor to traverse bit
 */

static inline TrieVisitorReturnCode
TraverseVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                      TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData*)visitor->_data;
   CBTBitmapAccessBitCB cb =
      (CBTBitmapAccessBitCB)data->_cb;

   uint8 i;
   uint8 byte, bit;
   uint8 toByte, toBit;
   uint8 *bitmap;
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   GET_BITMAP_BYTE_BIT(toOffset, toByte, toBit);

   bitmap = (uint8 *)(*pNode)->_bitmap;
   for (i = byte, nodeAddr += i * 8 ; i <= toByte ; ++i, nodeAddr += 8) {
      uint8 c = bitmap[i];
      uint8 b = (i == byte) ? bit : 0;
      uint8 e = (i == toByte) ? toBit : 7;
      c >>= b;
      while (c > 0 && b <= e) {
         if ((c & 0x1) > 0) {
            if (!cb(data->_cbData, nodeAddr+b)) {
               return TRIE_VISITOR_RET_ABORT;
            }
         }
         c >>= 1;
         ++b;
      }
   }

   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
TraverseVisitCollapsedNode(BlockTrackingSparseBitmapVisitor *visitor,
                           uint64 nodeAddr, uint8 height,
                           uint64 fromAddr, uint64 toAddr,
                           TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData*)visitor->_data;
   CBTBitmapAccessBitCB cb =
      (CBTBitmapAccessBitCB)data->_cb;
   uint64 nodeMaxAddr = NODE_MAX_ADDR(nodeAddr, height);
   uint64 start, end;

   ASSERT(fromAddr <= toAddr);

   start = (fromAddr >= nodeAddr) ? fromAddr : nodeAddr;
   end = (toAddr <= nodeMaxAddr) ? toAddr : nodeMaxAddr;
   for ( ; start <= end ; ++start) {
      if (!cb(data->_cbData, start)) {
         return TRIE_VISITOR_RET_ABORT;
      }
   }
   return TRIE_VISITOR_RET_CONT;
}


/**
 * A visitor to traverse extent
 */


/*
 * The callback data for traverse extents.
 */

typedef struct {
   uint64 _extStart;
   uint64 _extEnd;
   void *_extHdlData;
} GetExtentsData;

static inline TrieVisitorReturnCode
TraverseExtVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                         TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData*)visitor->_data;
   GetExtentsData *extData = (GetExtentsData*)data->_cbData;
   CBTBitmapAccessExtentCB cb = (CBTBitmapAccessExtentCB)data->_cb;

   uint8 i;
   uint8 byte, bit;
   uint8 toByte, toBit;
   uint8 *bitmap;
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   GET_BITMAP_BYTE_BIT(toOffset, toByte, toBit);


   bitmap = (uint8 *)(*pNode)->_bitmap;
   for (i = byte, nodeAddr += i * 8 ; i <= toByte ; ++i, nodeAddr += 8) {
      uint8 c = bitmap[i];
      uint8 b = (i == byte) ? bit : 0;
      uint8 e = (i == toByte) ? toBit : 7;
      c >>= b;
      while ((c > 0 || extData->_extStart != -1) && b <= e) {
         if ((c & 0x1) > 0) {
            // bit is set
            if (extData->_extStart == -1) {
               extData->_extStart = nodeAddr+b;
            }
            extData->_extEnd = nodeAddr+b;
         } else {
            // bit is unset
            if (extData->_extStart != -1) {
               if (!cb(extData->_extHdlData,
                       extData->_extStart, extData->_extEnd)) {
                  return TRIE_VISITOR_RET_ABORT;
               }
               extData->_extStart = -1;
               extData->_extEnd = -1;
            }
         }
         c >>= 1;
         ++b;
      }
   }
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
TraverseExtVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 nodeAddr, uint8 height,
                         uint64 fromAddr, uint64 toAddr,
                         TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData*)visitor->_data;
   GetExtentsData *extData = (GetExtentsData*)data->_cbData;
   CBTBitmapAccessExtentCB cb = (CBTBitmapAccessExtentCB)data->_cb;

   if (extData->_extStart != -1) {
      if (!cb(extData->_extHdlData, extData->_extStart, extData->_extEnd)) {
         return TRIE_VISITOR_RET_ABORT;
      }
      extData->_extStart = -1;
      extData->_extEnd = -1;
   }
   return TRIE_VISITOR_RET_SKIP_CHILDREN;
}

static inline TrieVisitorReturnCode
TraverseExtVisitCollapsedNode(BlockTrackingSparseBitmapVisitor *visitor,
                              uint64 nodeAddr, uint8 height,
                              uint64 fromAddr, uint64 toAddr,
                              TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData*)visitor->_data;
   GetExtentsData *extData = (GetExtentsData*)data->_cbData;
   CBTBitmapAccessExtentCB cb = (CBTBitmapAccessExtentCB)data->_cb;

   uint64 nodeMaxAddr = NODE_MAX_ADDR(nodeAddr, height);

   ASSERT(fromAddr <= toAddr);
   if (fromAddr >= nodeAddr) {
      // the begin of traverse
      ASSERT(extData->_extStart == -1);
      extData->_extStart = fromAddr;
   } else {
      if (extData->_extStart == -1) {
         extData->_extStart = nodeAddr;
      }
   }
   if (toAddr <= nodeMaxAddr) {
      // the end of traverse
      ASSERT(extData->_extStart != -1);
      if (!cb(extData->_extHdlData, extData->_extStart, toAddr)) {
         return TRIE_VISITOR_RET_ABORT;
      }
      extData->_extStart = -1;
      extData->_extEnd = -1;
   } else {
      extData->_extEnd = nodeMaxAddr;
   }
   return TRIE_VISITOR_RET_CONT;
}


/**
 *  A vistor to update statistics
 */

static inline TrieVisitorReturnCode
UpdateStatVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 nodeAddr, uint8 height,
                         uint64 fromAddr, uint64 toAddr,
                         TrieNode *pNode)
{
   if (IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(visitor->_stat->_flag)) {
      visitor->_stat->_memoryInUse += sizeof(**pNode);
   }
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
UpdateStatVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                        uint64 nodeAddr, uint16 fromOffset,
                        uint16 toOffset, TrieNode *pNode)
{
   TrieStatistics *stat = visitor->_stat;
   if (IS_TRIE_STAT_FLAG_BITSET_ON(stat->_flag)) {
      stat->_totalSet += TrieGetSetBitsInLeaf(*pNode, fromOffset, toOffset);
   }
   if (IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(stat->_flag)) {
      stat->_memoryInUse += sizeof(**pNode);
   }
   if (IS_TRIE_STAT_FLAG_COUNT_STREAM_ITEM_ON(stat->_flag)) {
      stat->_streamItemCount++;
   }
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
UpdateStateVisitCollapsedNode(BlockTrackingSparseBitmapVisitor *visitor,
                              uint64 nodeAddr, uint8 height,
                              uint64 fromAddr, uint64 toAddr,
                              TrieNode *pNode)
{
   TrieStatistics *stat = visitor->_stat;
   if (IS_TRIE_STAT_FLAG_BITSET_ON(stat->_flag)) {
      stat->_totalSet += NODE_MAX_ADDR(nodeAddr, height) - nodeAddr + 1;
   }
   if (IS_TRIE_STAT_FLAG_COUNT_STREAM_ITEM_ON(stat->_flag)) {
      stat->_streamItemCount++;
   }
   return TRIE_VISITOR_RET_CONT;
}


/**
 *  A visitor to deserialize
 */

static inline TrieVisitorReturnCode
DeserializeVisitNode(BlockTrackingSparseBitmapVisitor *visitor,
                     uint64 nodeAddr, uint8 height,
                     TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData *)visitor->_data;
   const BlockTrackingSparseBitmapStream *stream =
      (const BlockTrackingSparseBitmapStream*)data->_cb;
   const BlockTrackingSparseBitmapStream *streamEnd =
      (const BlockTrackingSparseBitmapStream*)data->_cbData;
   uint64 targetNodeAddr;
   uint64 maxNodeAddr;
   uint8 targetHeight = 0;
   Bool isTargetCollapsed = FALSE;

   // skip all items that before the node
   // they should be covered by previous collapsed node
   while (TRUE) {
      if (stream == streamEnd || stream->_nodeOffset == STREAM_ITEM_END) {
         return TRIE_VISITOR_RET_END;
      }

      if (stream->_nodeOffset == STREAM_ITEM_COLLAPSED_NODE) {
         // collapsed node
         isTargetCollapsed = TRUE;
         targetNodeAddr = stream->_payLoad._collapsedNode._addr;
         targetHeight = stream->_payLoad._collapsedNode._height;
      } else {
         // leaf node
         targetNodeAddr = stream->_nodeOffset << ADDR_BITS_IN_LEAF;
      }
      if (targetNodeAddr >= nodeAddr) {
         break; // target is not less than node addree then jump out
      }
      // go to the next item of stream
      ++stream;
   }

   maxNodeAddr = NODE_MAX_ADDR(nodeAddr, height);
   // target node address is bigger than the max of current one
   // so skip all sub tries (children).
   if (targetNodeAddr > maxNodeAddr) {
      return TRIE_VISITOR_RET_SKIP_CHILDREN;
   }

   // NOW, target node is in range of current one

   // target is collapsed and the current node is at the same address and height
   // so make current one collapsed and skip all children.
   if (isTargetCollapsed &&
       targetNodeAddr == nodeAddr && targetHeight == height) {
      if (!TrieIsCollapsedNode(*pNode)) {
         // free the trie
         BlockTrackingSparseBitmapVisitor deleteTrie = {
            DeleteLeafNode,
            NULL,
            DeleteInnerNode,
            NULL,
            DeleteCollapsedNode,
            NULL,
            visitor->_stat
         };
         TrieAccept(pNode, nodeAddr,  height, 0, -1, &deleteTrie);
         *pNode = NULL;
         TrieCollapseNode(pNode, nodeAddr, height, visitor->_stat);
      }
      // go to the next item of stream
      ++stream;
      data->_cb = (void *)stream;
      return TRIE_VISITOR_RET_SKIP_CHILDREN;
   }

   ASSERT(NODE_MAX_ADDR(targetNodeAddr, targetHeight) <= maxNodeAddr);

   if (TrieIsCollapsedNode(*pNode)) {
      // collapsed node
      // to the next stream
      ++stream;
      data->_cb = (void *)stream;
   } else if (*pNode == NULL) {
      // access null node
      if ((*pNode = AllocateTrieNode(visitor->_stat, height == 0)) == NULL) {
         if (visitor->_stat != NULL &&
             IS_TRIE_STAT_FLAG_OOM_AS_COLLAPSED_ON(visitor->_stat->_flag)) {
            // cannot allocate memory
            // make a collapsed node
            TrieCollapseNode(pNode, nodeAddr, height, visitor->_stat);
         } else {
            return TRIE_VISITOR_RET_OUT_OF_MEM;
         }
      }
   } else if (height == 0) {
      // leaf node
      TrieLeafNodeMergeFlatBitmap(*pNode,
                                  (const char *)stream->_payLoad._node._bitmap,
                                  visitor->_stat);
      if (TrieIsFullNode(*pNode)) {
         TrieCollapseNode(pNode, nodeAddr, height, visitor->_stat);
      }
      // to the next stream
      ++stream;
      data->_cb = (void *)stream;
   }
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
DeserializeVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 nodeAddr, uint8 height,
                         uint64 fromAddr, uint64 toAddr,
                         TrieNode *pNode)
{
   return DeserializeVisitNode(visitor, nodeAddr, height, pNode);
}

static inline TrieVisitorReturnCode
DeserializePreVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                             uint64 nodeAddr, uint8 height,
                             uint64 fromAddr, uint64 toAddr,
                             TrieNode *pNode)
{
   return DeserializeVisitNode(visitor, nodeAddr, height, pNode);
}

static inline TrieVisitorReturnCode
DeserializePostVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                              uint64 nodeAddr, uint8 height,
                              uint64 fromAddr, uint64 toAddr,
                              TrieNode *pNode)
{
   // bottom up collapse
   if (TrieIsFullNode(*pNode)) {
      TrieCollapseNode(pNode, nodeAddr, height, visitor->_stat);
   }
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
DeserializeVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 nodeAddr,
                         uint16 fromOffset, uint16 toOffset,
                         TrieNode *pNode)
{
   return DeserializeVisitNode(visitor, nodeAddr, 0, pNode);
}

static inline TrieVisitorReturnCode
DeserializeVisitCollapsedNode(BlockTrackingSparseBitmapVisitor *visitor,
                              uint64 nodeAddr, uint8 height,
                              uint64 fromAddr, uint64 toAddr,
                              TrieNode *pNode)
{
   return DeserializeVisitNode(visitor, nodeAddr, height, pNode);
}


/**
 *  A vistor to serialize
 */

static inline TrieVisitorReturnCode
SerializeVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                       uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                       TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData *)visitor->_data;
   BlockTrackingSparseBitmapStream *stream =
      (BlockTrackingSparseBitmapStream *)data->_cb;
   BlockTrackingSparseBitmapStream *streamEnd =
      (BlockTrackingSparseBitmapStream *)data->_cbData;
   uint16 nodeOffset = nodeAddr >> ADDR_BITS_IN_LEAF;

   if (stream != streamEnd) {
      stream->_nodeOffset = nodeOffset;
      memcpy(stream->_payLoad._node._bitmap,
             (*pNode)->_bitmap, sizeof(**pNode));
      // to the next stream
      ++stream;
      data->_cb = (void *)stream;
      return TRIE_VISITOR_RET_CONT;
   }
   return TRIE_VISITOR_RET_OVERFLOW;
}

static inline TrieVisitorReturnCode
SerializeVisitCollapsedNode(BlockTrackingSparseBitmapVisitor *visitor,
                            uint64 nodeAddr, uint8 height,
                            uint64 fromAddr, uint64 toAddr,
                            TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData *)visitor->_data;
   BlockTrackingSparseBitmapStream *stream =
      (BlockTrackingSparseBitmapStream *)data->_cb;
   BlockTrackingSparseBitmapStream *streamEnd =
      (BlockTrackingSparseBitmapStream *)data->_cbData;

   if (stream != streamEnd) {
      // magic number to indicate a collapsed node
      stream->_nodeOffset = STREAM_ITEM_COLLAPSED_NODE;
      // store node address and height
      stream->_payLoad._collapsedNode._addr = nodeAddr;
      stream->_payLoad._collapsedNode._height = height;
      ++stream;
      data->_cb = (void *)stream;
      return TRIE_VISITOR_RET_CONT;
   }
   return TRIE_VISITOR_RET_OVERFLOW;
}


////////////////////////////////////////////////////////////////////////////////
//   CBTBitmap Sparse Bitmap Implementation Functions
////////////////////////////////////////////////////////////////////////////////


/*
 *-----------------------------------------------------------------------------
 *
 * BlockTrackingSparseBitmapTranslateTrieRetCode  --
 *
 *    Translate the trie return code to CBT bitmap error code.
 *
 * Parameter:
 *    trieRetCode - input. The trie return code.
 *
 * Results:
 *    The CBT bitmap error code.
 *
 *-----------------------------------------------------------------------------
 */

static CBTBitmapError
BlockTrackingSparseBitmapTranslateTrieRetCode(TrieVisitorReturnCode trieRetCode)
{
   CBTBitmapError ret = CBT_BMAP_ERR_FAIL;
   switch (trieRetCode) {
      case TRIE_VISITOR_RET_ABORT:
      case TRIE_VISITOR_RET_SKIP_CHILDREN:
      case TRIE_VISITOR_RET_CONT:
         ret = CBT_BMAP_ERR_FAIL;
         break;
      case TRIE_VISITOR_RET_END:
         ret = CBT_BMAP_ERR_OK;
         break;
      case TRIE_VISITOR_RET_OUT_OF_MEM:
         ret = CBT_BMAP_ERR_OUT_OF_MEM;
         break;
      case TRIE_VISITOR_RET_OVERFLOW:
         ret = CBT_BMAP_ERR_OUT_OF_RANGE;
         break;
      default:
         break;
   }
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BlockTrackingSparseBitmapAccept --
 *
 *    Sparse bitmap accepts a visitor.
 *
 * Parameter:
 *    bitmap - input. CBT bitmap instance.
 *    fromAddr - input. The start address in the scope.
 *    toAddre - input. The end address in the scope.
 *    visitor - input. A pointer to the visitor instance.
 *
 * Results:
 *    CBT bitmap error code.
 *
 *-----------------------------------------------------------------------------
 */

static CBTBitmapError
BlockTrackingSparseBitmapAccept(CBTBitmap bitmap,
                                uint64 fromAddr, uint64 toAddr,
                                BlockTrackingSparseBitmapVisitor *visitor)
{
   uint8 i = TrieMaxHeight(fromAddr);
   uint64 nodeAddr;
   TrieVisitorReturnCode trieRetCode = TRIE_VISITOR_RET_ABORT;
   if (!TrieIndexValidation(i)) {
      return CBT_BMAP_ERR_INVALID_ADDR;
   }

   for (nodeAddr = (i == 0) ? 0 : (NODE_MAX_ADDR(0, i-1) + 1);
        i < MAX_NUM_TRIES && nodeAddr <= toAddr;
        nodeAddr = NODE_MAX_ADDR(0, i) + 1, ++i) {
      trieRetCode =
         TrieAccept(&bitmap->_tries[i], nodeAddr, i, fromAddr, toAddr, visitor);
      if (trieRetCode != TRIE_VISITOR_RET_CONT &&
          trieRetCode != TRIE_VISITOR_RET_SKIP_CHILDREN) {
         break;
      }
   }
   if ((i == MAX_NUM_TRIES || nodeAddr > toAddr) &&
       (trieRetCode == TRIE_VISITOR_RET_SKIP_CHILDREN ||
        trieRetCode == TRIE_VISITOR_RET_CONT)) {
      trieRetCode = TRIE_VISITOR_RET_END;
   }
   return BlockTrackingSparseBitmapTranslateTrieRetCode(trieRetCode);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BlockTrackingSparseBitmapDeleteTries --
 *
 *    Delete all tries in the bitmap.
 *
 * Parameter:
 *    bitmap - input/output. CBT bitmap instance.
 *
 * Results:
 *    CBT bitmap error code.
 *
 *-----------------------------------------------------------------------------
 */

static CBTBitmapError
BlockTrackingSparseBitmapDeleteTries(CBTBitmap bitmap)
{
   CBTBitmapError ret;
   BlockTrackingSparseBitmapVisitor deleteTrie = {
      DeleteLeafNode,
      NULL,
      DeleteInnerNode,
      NULL,
      DeleteCollapsedNode,
      NULL,
      (IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(bitmap->_stat._flag)) ?
         &bitmap->_stat : NULL
   };
   ret = BlockTrackingSparseBitmapAccept(bitmap, 0, -1, &deleteTrie);
   if (IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(bitmap->_stat._flag)) {
      ASSERT(bitmap->_stat._memoryInUse == sizeof(*bitmap));
   }
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BlockTrackingSparseBitmapSetBit --
 *
 *    Set a bit in the sparse bitmap.
 *
 * Parameter:
 *    bitmap - input/output. CBT bitmap instance.
 *    addr - input. The address of the bit.
 *    isSetBefor - output. The original value of the bit.
 *
 * Results:
 *    CBT bitmap error code.
 *
 *-----------------------------------------------------------------------------
 */

static CBTBitmapError
BlockTrackingSparseBitmapSetBit(CBTBitmap bitmap, uint64 addr,
                                Bool *isSetBefore)
{
   CBTBitmapError ret;
   Bool isSet = FALSE;
   BlockTrackingSparseBitmapVisitor setBit = {
      SetBitVisitLeafNode,
      NULL,
      SetBitPostVisitInnerNode,
      AllocateNodeVisitNullNode,
      NULL,
      &isSet,
      (TRIE_STAT_FLAG_IS_NULL(bitmap->_stat._flag)) ? NULL : &bitmap->_stat
   };

   ret = BlockTrackingSparseBitmapAccept(bitmap, addr, addr, &setBit);

   if (ret == CBT_BMAP_ERR_OK) {
      if (isSetBefore != NULL) {
         *isSetBefore = isSet;
      }
   }
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BlockTrackingSparseBitmapSetBits  --
 *
 *    Set bits in a range in the sparse bitmap.
 *
 * Parameter:
 *    bitmap - input/output. CBT bitmap instance.
 *    fromAddr - input. The start address in the scope.
 *    toAddre - input. The end address in the scope.
 *
 * Results:
 *    CBT bitmap error code.
 *
 *-----------------------------------------------------------------------------
 */

static CBTBitmapError
BlockTrackingSparseBitmapSetBits(CBTBitmap bitmap,
                                 uint64 fromAddr, uint64 toAddr)
{
   BlockTrackingSparseBitmapVisitor setBits = {
      SetBitsVisitLeafNode,
      SetBitsPreVisitInnerNode,
      SetBitsPostVisitInnerNode,
      SetBitsVisitNullNode,
      NULL,
      bitmap->_tries,
      (TRIE_STAT_FLAG_IS_NULL(bitmap->_stat._flag)) ? NULL : &bitmap->_stat
   };

   return BlockTrackingSparseBitmapAccept(bitmap, fromAddr, toAddr, &setBits);
}

static CBTBitmapError
BlockTrackingSparseBitmapQueryBit(CBTBitmap bitmap,
                                  uint64 addr, Bool *isSetBefore)
{
   BlockTrackingSparseBitmapVisitor queryBit = {
      QueryBitVisitLeafNode,
      NULL,
      NULL,
      QueryBitVisitNullNode,
      QueryBitVisitCollapsedNode,
      isSetBefore,
      NULL
   };
   return BlockTrackingSparseBitmapAccept(bitmap, addr, addr, &queryBit);
}

static CBTBitmapError
BlockTrackingSparseBitmapTraverseByBit(CBTBitmap bitmap,
                                       uint64 fromAddr, uint64 toAddr,
                                       CBTBitmapAccessBitCB cb,
                                       void *cbData)
{
   BlockTrackingBitmapCallbackData data = {cb, cbData};
   BlockTrackingSparseBitmapVisitor traverse = {
      TraverseVisitLeafNode,
      NULL,
      NULL,
      NULL,
      TraverseVisitCollapsedNode,
      &data,
      NULL
   };
   return BlockTrackingSparseBitmapAccept(bitmap, fromAddr, toAddr, &traverse);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BlockTrackingSparseBitmapTraverseByExtent --
 *
 *    Traverse the extents of the sparse bitmap.
 *
 * Parameter:
 *    bitmap - input/output. CBT bitmap instance.
 *    fromAddr - input. The start address in the scope.
 *    toAddre - input. The end address in the scope.
 *    cb - input. The callback fro each extent.
 *    cbData - input. The callback data.
 *
 * Results:
 *    CBT bitmap error code.
 *
 *-----------------------------------------------------------------------------
 */

static CBTBitmapError
BlockTrackingSparseBitmapTraverseByExtent(
                                    CBTBitmap bitmap,
                                    uint64 fromAddr, uint64 toAddr,
                                    CBTBitmapAccessExtentCB cb,
                                    void *cbData)
{
   GetExtentsData extData = {-1, -1, cbData};
   BlockTrackingBitmapCallbackData data = {cb, &extData};
   BlockTrackingSparseBitmapVisitor traverse = {
      TraverseExtVisitLeafNode,
      NULL,
      NULL,
      TraverseExtVisitNullNode,
      TraverseExtVisitCollapsedNode,
      &data,
      NULL
   };
   CBTBitmapError err;
   err = BlockTrackingSparseBitmapAccept(bitmap, fromAddr, toAddr, &traverse);
   if (err != CBT_BMAP_ERR_OK) {
      return err;
   }
   if (extData._extStart != -1) {
      if (!cb(cbData, extData._extStart, extData._extEnd)) {
         return CBT_BMAP_ERR_FAIL;
      }
   }
   return CBT_BMAP_ERR_OK;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BlockTrackingSparseBitmapUpdateStatistics --
 *
 *    Update all statistics of the sparse bitmap.
 *
 * Parameter:
 *    bitmap - input/output. CBT bitmap instance.
 *
 * Results:
 *    CBT bitmap error code.
 *
 *-----------------------------------------------------------------------------
 */

static CBTBitmapError
BlockTrackingSparseBitmapUpdateStatistics(CBTBitmap bitmap)
{
   uint16 flag;
   BlockTrackingSparseBitmapVisitor updateStat = {
      UpdateStatVisitLeafNode,
      UpdateStatVisitInnerNode,
      NULL,
      NULL,
      UpdateStateVisitCollapsedNode,
      NULL,
      &bitmap->_stat
   };
   ASSERT(!TRIE_STAT_FLAG_IS_NULL(bitmap->_stat._flag));
   flag = bitmap->_stat._flag;
   memset(&bitmap->_stat, 0, sizeof(bitmap->_stat));
   bitmap->_stat._memoryInUse = sizeof(*bitmap);
   bitmap->_stat._flag = flag;
   return BlockTrackingSparseBitmapAccept(bitmap, 0, -1, &updateStat);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BlockTrackingSparseBitmapDeserialize --
 *
 *    Deserialize the sparse bitmap.
 *
 * Parameter:
 *    bitmap - input/output. CBT bitmap instance.
 *    stream - input. The input stream.
 *    streamLen - input. The length of input stream.
 *
 * Results:
 *    CBT bitmap error code.
 *
 *-----------------------------------------------------------------------------
 */

static CBTBitmapError
BlockTrackingSparseBitmapDeserialize(CBTBitmap bitmap,
                                     const char *stream, uint32 streamLen)
{
   BlockTrackingSparseBitmapStream *begin =
      (BlockTrackingSparseBitmapStream *)stream;
   uint32 len = streamLen / sizeof(*begin);
   BlockTrackingSparseBitmapStream *end = begin + len;
   BlockTrackingBitmapCallbackData data = {begin, end};
   BlockTrackingSparseBitmapVisitor deserialize = {
      DeserializeVisitLeafNode,
      DeserializePreVisitInnerNode,
      DeserializePostVisitInnerNode,
      DeserializeVisitNullNode,
      DeserializeVisitCollapsedNode,
      &data,
      (TRIE_STAT_FLAG_IS_NULL(bitmap->_stat._flag)) ? NULL : &bitmap->_stat
   };
   return BlockTrackingSparseBitmapAccept(bitmap, 0, -1, &deserialize);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BlockTrackingSparseBitmapSerialize --
 *
 *    Serialize the sparse bitmap.
 *
 * Parameter:
 *    bitmap - input. CBT bitmap instance.
 *    stream - output. The output stream.
 *    streamLen - input. The length of output stream.
 *
 * Results:
 *    CBT bitmap error code.
 *
 *-----------------------------------------------------------------------------
 */

static CBTBitmapError
BlockTrackingSparseBitmapSerialize(CBTBitmap bitmap,
                                   char *stream, uint32 streamLen)
{
   CBTBitmapError ret;
   BlockTrackingSparseBitmapStream *begin =
      (BlockTrackingSparseBitmapStream *)stream;
   uint32 len = streamLen / sizeof(*begin);
   BlockTrackingSparseBitmapStream *end = begin + len;
   BlockTrackingBitmapCallbackData data = {begin, end};
   BlockTrackingSparseBitmapVisitor serialize = {
      SerializeVisitLeafNode,
      NULL,
      NULL,
      NULL,
      SerializeVisitCollapsedNode,
      &data,
      NULL
   };
   ret = BlockTrackingSparseBitmapAccept(bitmap, 0, -1, &serialize);
   // append a terminator as the end of stream if the stream is not exhausted
   if (ret == CBT_BMAP_ERR_OK) {
      begin = (BlockTrackingSparseBitmapStream *)data._cb;
      if (begin != end) {
         begin->_nodeOffset = STREAM_ITEM_END;
      }
   }
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * BlockTrackingSparseBitmapMakeTrieStatFlag --
 *
 *    Translate the CBT creation flags to be Trie stat flags.
 *
 * Parameter:
 *    mode - input. CBT creation flags.
 *
 * Results:
 *    Trie stat flags.
 *
 *-----------------------------------------------------------------------------
 */

static uint16
BlockTrackingSparseBitmapMakeTrieStatFlag(uint16 mode)
{
   uint16 flag = 0;
   if (mode & CBT_BMAP_MODE_FAST_SET) {
      flag = 0;
   }
   if (mode & CBT_BMAP_MODE_FAST_MERGE) {
      flag = 0;
   }
   if (mode & CBT_BMAP_MODE_FAST_SERIALIZE) {
      flag |= TRIE_STAT_FLAG_COUNT_STREAM_ITEM;
   }
   if (mode & CBT_BMAP_MODE_FAST_STATISTIC) {
      flag |= TRIE_STAT_FLAG_BITSET;
      flag |= TRIE_STAT_FLAG_MEMORY_ALLOC;
   }
   if (mode & CBT_BMAP_MODE_NO_MEMORY_FAIL) {
      flag |= TRIE_STAT_FLAG_OOM_AS_COLLAPSED;
   }
   return flag;
}


////////////////////////////////////////////////////////////////////////////////
//   Public Interface (reference cbtBitmap.h)
////////////////////////////////////////////////////////////////////////////////


CBTBitmapError
CBTBitmap_Init(CBTBitmapAllocator *alloc)
{
   if (g_IsInited) {
      return CBT_BMAP_ERR_REINIT;
   }
   if (alloc != NULL) {
      if (alloc->allocate == NULL || alloc->deallocate == NULL) {
         return CBT_BMAP_ERR_INVALID_ARG;
      }
      g_Allocator = *alloc;
   } else {
      g_Allocator.allocate = CBTBitmapDefaultAlloc;
      g_Allocator.deallocate = CBTBitmapDefaultFree;
      g_Allocator._data = NULL;
   }
   g_IsInited = TRUE;
   return CBT_BMAP_ERR_OK;
}

void
CBTBitmap_Exit()
{
   ASSERT(g_IsInited);
   memset(&g_Allocator, 0, sizeof g_Allocator);
   g_IsInited = FALSE;
}

CBTBitmapError
CBTBitmap_Create(CBTBitmap *bitmap, uint16 mode)
{
   if (bitmap == NULL) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }
   *bitmap = AllocateBitmap();
   if (*bitmap == NULL) {
      return CBT_BMAP_ERR_OUT_OF_MEM;
   }
   (*bitmap)->_stat._flag = BlockTrackingSparseBitmapMakeTrieStatFlag(mode);
   if (IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON((*bitmap)->_stat._flag)) {
      (*bitmap)->_stat._memoryInUse = sizeof(**bitmap);
   }
   return CBT_BMAP_ERR_OK;
}

void
CBTBitmap_Destroy(CBTBitmap bitmap)
{
   if (bitmap != NULL) {
      BlockTrackingSparseBitmapDeleteTries(bitmap);
      FreeBitmap(bitmap);
   }
}

CBTBitmapError
CBTBitmap_SetAt(CBTBitmap bitmap, uint64 addr, Bool *oldValue)
{
   ASSERT(bitmap != NULL);
   return BlockTrackingSparseBitmapSetBit(bitmap, addr, oldValue);
}

CBTBitmapError
CBTBitmap_SetInRange(CBTBitmap bitmap, uint64 fromAddr, uint64 toAddr)
{
   ASSERT(bitmap != NULL);
   if (toAddr < fromAddr) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }

   return BlockTrackingSparseBitmapSetBits(bitmap, fromAddr, toAddr);
}

CBTBitmapError
CBTBitmap_IsSet(CBTBitmap bitmap, uint64 addr, Bool *isSet)
{
   ASSERT(bitmap != NULL);
   if (isSet == NULL) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }
   return BlockTrackingSparseBitmapQueryBit(bitmap, addr, isSet);
}

CBTBitmapError
CBTBitmap_TraverseByBit(CBTBitmap bitmap, uint64 fromAddr, uint64 toAddr,
                        CBTBitmapAccessBitCB cb, void *cbData)
{
   ASSERT(bitmap != NULL);
   if (cb == NULL || toAddr < fromAddr) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }
   return BlockTrackingSparseBitmapTraverseByBit(bitmap, fromAddr, toAddr,
                                                 cb, cbData);
}

CBTBitmapError
CBTBitmap_TraverseByExtent(CBTBitmap bitmap, uint64 fromAddr, uint64 toAddr,
                           CBTBitmapAccessExtentCB cb, void *cbData)
{
   ASSERT(bitmap != NULL);
   if (cb == NULL || toAddr < fromAddr) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }
   return BlockTrackingSparseBitmapTraverseByExtent(bitmap, fromAddr, toAddr,
                                                    cb, cbData);
}

void
CBTBitmap_Swap(CBTBitmap bitmap1, CBTBitmap bitmap2)
{
   struct CBTBitmap tmp;
   ASSERT(bitmap1 != NULL);
   ASSERT(bitmap2 != NULL);

   if (bitmap1 != bitmap2) {
      memcpy(&tmp, bitmap1, sizeof(tmp));
      memcpy(bitmap1, bitmap2, sizeof(tmp));
      memcpy(bitmap2, &tmp, sizeof(tmp));
   }
}

CBTBitmapError
CBTBitmap_Merge(CBTBitmap dest, CBTBitmap src)
{
   uint8 i;
   TrieStatistics *stat;
   TrieVisitorReturnCode trieRetCode = TRIE_VISITOR_RET_CONT;
   uint64 nodeAddr = 0;

   ASSERT(dest != NULL);
   if (src == NULL) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }

   stat = (TRIE_STAT_FLAG_IS_NULL(dest->_stat._flag)) ? NULL : &dest->_stat;
   for (i = 0; i < MAX_NUM_TRIES;
        nodeAddr = NODE_MAX_ADDR(0, i) + 1, ++i) {
      trieRetCode = TrieMerge(&dest->_tries[i], src->_tries[i], nodeAddr, i,
                              stat);
      if (trieRetCode != TRIE_VISITOR_RET_CONT &&
          trieRetCode != TRIE_VISITOR_RET_SKIP_CHILDREN) {
         break;
      }
   }
   if (i == MAX_NUM_TRIES &&
       (trieRetCode == TRIE_VISITOR_RET_SKIP_CHILDREN ||
        trieRetCode == TRIE_VISITOR_RET_CONT)) {
      trieRetCode = TRIE_VISITOR_RET_END;
   }
   return BlockTrackingSparseBitmapTranslateTrieRetCode(trieRetCode);
}


CBTBitmapError
CBTBitmap_Deserialize(CBTBitmap bitmap, const char *stream, uint32 streamLen)
{
   ASSERT(bitmap != NULL);
   if (stream == NULL || streamLen == 0) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }

   return BlockTrackingSparseBitmapDeserialize(bitmap, stream, streamLen);
}

CBTBitmapError
CBTBitmap_Serialize(CBTBitmap bitmap, char *stream, uint32 streamLen)
{
   ASSERT(bitmap != NULL);
   if (stream == NULL || streamLen == 0) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }

   return BlockTrackingSparseBitmapSerialize(bitmap, stream, streamLen);
}

CBTBitmapError
CBTBitmap_GetStreamMaxSize(uint64 maxAddr, uint32 *streamLen)
{
   if (streamLen == NULL) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }
   *streamLen = (maxAddr >> ADDR_BITS_IN_LEAF) + 1;
   if (*streamLen > MAX_NUM_LEAVES) {
      return CBT_BMAP_ERR_INVALID_ADDR;
   }
   ++(*streamLen); // need a terminator
   *streamLen *= sizeof(BlockTrackingSparseBitmapStream);
   return CBT_BMAP_ERR_OK;
}

uint32
CBTBitmap_GetStreamSize(CBTBitmap bitmap)
{
   uint16 streamItemCount;
   ASSERT(bitmap != NULL);
   if (!IS_TRIE_STAT_FLAG_COUNT_STREAM_ITEM_ON(bitmap->_stat._flag)) {
      TrieStatistics stat = bitmap->_stat;
      memset(&bitmap->_stat, 0, sizeof(bitmap->_stat));
      bitmap->_stat._flag |= TRIE_STAT_FLAG_COUNT_STREAM_ITEM;
      BlockTrackingSparseBitmapUpdateStatistics(bitmap);
      streamItemCount = bitmap->_stat._streamItemCount;
      bitmap->_stat = stat;
   } else {
      streamItemCount = bitmap->_stat._streamItemCount;
   }
   ASSERT(streamItemCount <= MAX_NUM_LEAVES);
   ++streamItemCount; // need a terminator
   return streamItemCount * sizeof(BlockTrackingSparseBitmapStream);
}

uint32
CBTBitmap_GetBitCount(CBTBitmap bitmap)
{
   uint32 bitCount;
   ASSERT(bitmap != NULL);
   if (!IS_TRIE_STAT_FLAG_BITSET_ON(bitmap->_stat._flag)) {
      TrieStatistics stat = bitmap->_stat;
      memset(&bitmap->_stat, 0, sizeof(bitmap->_stat));
      bitmap->_stat._flag |= TRIE_STAT_FLAG_BITSET;
      BlockTrackingSparseBitmapUpdateStatistics(bitmap);
      bitCount = bitmap->_stat._totalSet;
      bitmap->_stat = stat;
   } else {
      bitCount = bitmap->_stat._totalSet;
   }
   return bitCount;
}

uint32
CBTBitmap_GetMemoryInUse(CBTBitmap bitmap)
{
   uint32 memoryInUse;
   ASSERT(bitmap != NULL);
   if (!IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(bitmap->_stat._flag)) {
      TrieStatistics stat = bitmap->_stat;
      memset(&bitmap->_stat, 0, sizeof(bitmap->_stat));
      bitmap->_stat._flag |= TRIE_STAT_FLAG_MEMORY_ALLOC;
      BlockTrackingSparseBitmapUpdateStatistics(bitmap);
      memoryInUse = bitmap->_stat._memoryInUse;
      bitmap->_stat = stat;
   } else {
      memoryInUse = bitmap->_stat._memoryInUse;
   }
   return memoryInUse;
}

uint64
CBTBitmap_GetCapacity()
{
   return MAX_NUM_LEAVES * (1ul << ADDR_BITS_IN_LEAF);
}

#ifdef CBT_BITMAP_UNITTEST
#include <stdio.h>
#define LEAF_NAME_PREFIX "Leaf"
#define INNER_NAME_PREFIX "Inner"
#define COLLAPSED_NAME_PREFIX "Collapsed"

#define CHILD_OFFSET(p, c) ((uint64)(c)-(uint64)(p))/sizeof(TrieNode)

#define NODE_NAME_LENGTH 64

typedef struct {
  char _parentName[NODE_NAME_LENGTH];
  TrieNode _parentNode;
} TraverseInfo;

typedef struct TraverseContext {
   TraverseInfo _info[MAX_NUM_TRIES];
   uint8 _size;
} TraverseContext;

static inline const char *
MakeNodeName(const char *prefix)
{
   static int id = 0;
   static char name[NODE_NAME_LENGTH];
   snprintf(name, sizeof name, "%s_%d", prefix, ++id);
   return name;
}

static inline TrieVisitorReturnCode
DumpVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                  uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                  TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData *)visitor->_data;
   CBTBitmapDumpCb *cb = (CBTBitmapDumpCb*)data->_cb;
   TraverseContext *ctx = (TraverseContext *)visitor->_stat;
   TraverseInfo *info = &ctx->_info[ctx->_size-1];
   const char *name = MakeNodeName(LEAF_NAME_PREFIX);
   if (!cb->AddLeafNode(data->_cbData, info->_parentName,
                        CHILD_OFFSET(info->_parentNode, pNode),
                        name, (*pNode)->_bitmap, sizeof (*pNode)->_bitmap)) {
      return TRIE_VISITOR_RET_ABORT;
   }
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
DumpPostVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                       uint64 nodeAddr, uint8 height,
                       uint64 fromAddr, uint64 toAddr,
                       TrieNode *pNode)
{
   TraverseContext *ctx = (TraverseContext *)visitor->_stat;
   ctx->_size--;
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
DumpPreVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                   uint64 nodeAddr, uint8 height,
                   uint64 fromAddr, uint64 toAddr,
                   TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData *)visitor->_data;
   CBTBitmapDumpCb *cb = (CBTBitmapDumpCb*)data->_cb;
   TraverseContext *ctx = (TraverseContext *)visitor->_stat;
   TraverseInfo *info = &ctx->_info[ctx->_size-1];
   TraverseInfo *newInfo = &ctx->_info[ctx->_size];
   const char *name = MakeNodeName(INNER_NAME_PREFIX);
   if (!cb->AddInnerNode(data->_cbData, info->_parentName,
                         CHILD_OFFSET(info->_parentNode, pNode),
                         name, NUM_TRIE_WAYS)) {
      return TRIE_VISITOR_RET_ABORT;
   }
   strncpy(newInfo->_parentName, name, sizeof newInfo->_parentName);
   newInfo->_parentNode = *pNode;
   ctx->_size++;

   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
DumpVisitCollapsedNode(BlockTrackingSparseBitmapVisitor *visitor,
                           uint64 nodeAddr, uint8 height,
                           uint64 fromAddr, uint64 toAddr,
                           TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData *)visitor->_data;
   CBTBitmapDumpCb *cb = (CBTBitmapDumpCb*)data->_cb;
   TraverseContext *ctx = (TraverseContext *)visitor->_stat;
   TraverseInfo *info = &ctx->_info[ctx->_size-1];
   if (!cb->AddCollapsedNode(data->_cbData, info->_parentName,
                             CHILD_OFFSET(info->_parentNode, pNode),
                             COLLAPSED_NAME_PREFIX, NUM_TRIE_WAYS)) {
      return TRIE_VISITOR_RET_ABORT;
   }
   return TRIE_VISITOR_RET_CONT;
}

static CBTBitmapError
BlockTrackingSparseBitmapDump(CBTBitmap bitmap,
                              CBTBitmapDumpCb *cb,
                              void *cbData)
{
   TraverseContext ctx;
   BlockTrackingBitmapCallbackData data = {cb, cbData};
   BlockTrackingSparseBitmapVisitor dump = {
      DumpVisitLeafNode,
      DumpPreVisitInnerNode,
      DumpPostVisitInnerNode,
      NULL,
      DumpVisitCollapsedNode,
      &data,
      (TrieStatistics*)&ctx
   };
   strcpy(ctx._info[0]._parentName, "root");
   ctx._info[0]._parentNode = (TrieNode)&bitmap->_tries[0];
   ctx._size = 1;
   if (!cb->AddRoot(cbData, MAX_NUM_TRIES)) {
      return TRIE_VISITOR_RET_ABORT;
   }
   return BlockTrackingSparseBitmapAccept(bitmap, 0, -1, &dump);
}

CBTBitmapError
CBTBitmap_Dump(CBTBitmap bitmap, CBTBitmapDumpCb *cb, void *data)
{
   ASSERT(bitmap != NULL);
   if (cb == NULL) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }

   return BlockTrackingSparseBitmapDump(bitmap, cb, data);
}

#endif
