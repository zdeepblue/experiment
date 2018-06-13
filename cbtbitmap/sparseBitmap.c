/* ***************************************************************************
 * Copyright 2018 VMware, Inc.  All rights reserved.
 * -- VMware Confidential
 * **************************************************************************/

/*
 * CBT Bitmap Sparse Bitmap implementation file
 *
 * It applies a Trie forest as a sparse bitmap to implement all public APIs.
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


// definition of maros
#define MAX_NUM_TRIES 6
#define ADDR_BITS_IN_LEAF 9
#define ADDR_BITS_IN_INNER_NODE 3
#define ADDR_BITS_IN_HEIGHT(h) (ADDR_BITS_IN_LEAF+(h-1)*ADDR_BITS_IN_INNER_NODE)
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
   ((h==0) ? ~LEAF_VALUE_MASK :  \
      ~(((uint64)1ull << ADDR_BITS_IN_HEIGHT(h)) - 1))
#define NODE_VALUE_MASK(h)  (~NODE_ADDR_MASK(h))

#define GET_NODE_WAYS(addr, h) ((addr>>ADDR_BITS_IN_HEIGHT(h)) & TRIE_WAY_MASK)

#define TRIE_STAT_FLAG_BITSET        1
#define TRIE_STAT_FLAG_MEMORY_ALLOC (1 << 1)
#define TRIE_STAT_FLAG_COUNT_LEAF   (1 << 2)

#define TRIE_STAT_FLAG_IS_NULL(f) ((f) == 0)
#define IS_TRIE_STAT_FLAG_BITSET_ON(f) ((f) & TRIE_STAT_FLAG_BITSET)
#define IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(f) ((f) & TRIE_STAT_FLAG_MEMORY_ALLOC)
#define IS_TRIE_STAT_FLAG_COUNT_LEAF_ON(f) ((f) & TRIE_STAT_FLAG_COUNT_LEAF)


// definition of data structures
typedef struct TrieStatistics {
   uint32 _totalSet;
   uint32 _memoryInUse;
   uint16 _leafCount;
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
   union TrieNode _node;
}
#ifndef CBT_BITMAP_UNITTEST
#include "vmware_pack_end.h"
#else
__attribute__((__packed__)) 
#endif
BlockTrackingSparseBitmapStream;


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
      uint64 nodeAddr, uint8 height, TrieNode node);

typedef TrieVisitorReturnCode (*VisitLeafNode) (
      struct BlockTrackingSparseBitmapVisitor *visitor,
      uint64 nodeAddr, uint16 fromOffset, uint16 toOffset, TrieNode node);

typedef TrieVisitorReturnCode (*VisitNullNode) (
      struct BlockTrackingSparseBitmapVisitor *visitor, uint64 nodeAddr,
      uint8 height, TrieNode *pNode);

typedef struct BlockTrackingSparseBitmapVisitor {
   VisitLeafNode _visitLeafNode;
   VisitInnerNode _beforeVisitInnerNode;
   VisitInnerNode _afterVisitInnerNode;
   VisitNullNode _visitNullNode;
   void *_data;
   TrieStatistics *_stat;
} BlockTrackingSparseBitmapVisitor;


// globals
static Bool g_IsInited;
static CBTBitmapAllocator g_Allocator;


////////////////////////////////////////////////////////////////////////////////
//   Allocate/Free Functions
////////////////////////////////////////////////////////////////////////////////


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
         if (isLeaf && IS_TRIE_STAT_FLAG_COUNT_LEAF_ON(stat->_flag)) {
            stat->_leafCount++;
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
 * Results:
 *    The trie node or NULL if error.
 *
 *-----------------------------------------------------------------------------
 */

static inline void
FreeTrieNode(TrieNode node, TrieStatistics *stat)
{
   if (stat != NULL && IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(stat->_flag)) {
      stat->_memoryInUse -= sizeof(*node);
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
 * Results:
 *    An allocated CBT bitmap without initialization.
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
TrieMerge(TrieNode *pDestNode, TrieNode srcNode, uint8 height,
          TrieStatistics *stat)
{
   TrieVisitorReturnCode ret = TRIE_VISITOR_RET_CONT;
   if (srcNode == NULL) {
      // nothing to merge from src
      return TRIE_VISITOR_RET_CONT;
   }
   if (*pDestNode == NULL) {
      *pDestNode = AllocateTrieNode(stat, height == 0);
      if (*pDestNode == NULL) {
         return TRIE_VISITOR_RET_OUT_OF_MEM;
      }
   }
   if (height == 0) {
      // leaf
      TrieLeafNodeMergeFlatBitmap(*pDestNode, srcNode->_bitmap, stat);
   } else {
      // inner node
      uint8 way;
      for (way = 0 ; way < NUM_TRIE_WAYS ; ++way) {
         ret = TrieMerge(&(*pDestNode)->_children[way],
                         srcNode->_children[way],
                         height-1, stat);
         if (ret != TRIE_VISITOR_RET_CONT &&
             ret != TRIE_VISITOR_RET_SKIP_CHILDREN) {
            return ret;
         }
      }
   }
   return ret;
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
TrieAccept(TrieNode *pNode, uint8 height, uint64 *fromAddr, uint64 toAddr,
           BlockTrackingSparseBitmapVisitor *visitor)
{
   TrieVisitorReturnCode ret = TRIE_VISITOR_RET_CONT;
   uint64 nodeAddr;
   ASSERT(*fromAddr <= toAddr);
   nodeAddr = *fromAddr & NODE_ADDR_MASK(height);
   if (*pNode == NULL) {
      if (visitor->_visitNullNode == NULL) {
         // ignore NULL node and continue the traverse
         goto exit;
      }
      if ((ret = visitor->_visitNullNode(visitor, nodeAddr, height, pNode))
            != TRIE_VISITOR_RET_CONT) {
         goto exit;
      }
   }
   if (height == 0) {
      // leaf
      uint16 fromOffset = *fromAddr & LEAF_VALUE_MASK;
      uint16 toOffset =
         (toAddr > (*fromAddr | LEAF_VALUE_MASK)) ?
                LEAF_VALUE_MASK : toAddr & LEAF_VALUE_MASK;
      if (visitor->_visitLeafNode != NULL) {
         if ((ret = visitor->_visitLeafNode(visitor, nodeAddr,
                                            fromOffset, toOffset, *pNode))
               != TRIE_VISITOR_RET_CONT) {
            goto exit;
         }
      }
   } else {
      // inner node
      uint8 way;
      if (visitor->_beforeVisitInnerNode != NULL) {
         if ((ret = visitor->_beforeVisitInnerNode(visitor, nodeAddr, height,
                                                   *pNode))
               != TRIE_VISITOR_RET_CONT) {
            goto exit;
         }
      }
      for (way = GET_NODE_WAYS(nodeAddr, height);
           way < NUM_TRIE_WAYS && *fromAddr <= toAddr;
           ++way) {
         ret = TrieAccept(&(*pNode)->_children[way], height-1, fromAddr, toAddr,
                          visitor);
         if (ret != TRIE_VISITOR_RET_CONT &&
             ret != TRIE_VISITOR_RET_SKIP_CHILDREN) {
            // fromAddr has been updated in the previous recursive call
            // so just return here
            return ret;
         }
      }
      if (visitor->_afterVisitInnerNode != NULL) {
         if ((ret = visitor->_afterVisitInnerNode(visitor, nodeAddr, height,
                                                  *pNode))
               != TRIE_VISITOR_RET_CONT) {
            goto exit;
         }
      }
   }
exit:
   // update fromAddr for next node in traverse
   *fromAddr = nodeAddr | NODE_VALUE_MASK(height+1);
   ++(*fromAddr);
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
 * A visitor to create NULL node
 */

static inline TrieVisitorReturnCode
AllocateNodeVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                          uint64 nodeAddr, uint8 height,
                          TrieNode *pNode)
{
   *pNode = AllocateTrieNode(visitor->_stat, height == 0);
   if (*pNode == NULL) {
      return TRIE_VISITOR_RET_OUT_OF_MEM;
   }
   return TRIE_VISITOR_RET_CONT;
}

/**
 * A visitor to query set bit
 */

static inline TrieVisitorReturnCode
QueryBitVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64 nodeAddr, uint8 height,
                      TrieNode *pNode)
{
   Bool *isSet = (Bool *)visitor->_data;
   *isSet = FALSE;
   return TRIE_VISITOR_RET_END;
}


static inline TrieVisitorReturnCode
QueryBitVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                      TrieNode node)
{
   Bool *isSet = (Bool *)visitor->_data;
   uint8 byte, bit;
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   *isSet = node->_bitmap[byte] & (1u << bit);
   return TRIE_VISITOR_RET_END;
}


/**
 * A visitor to set bit
 */

static inline TrieVisitorReturnCode
SetBitVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                    uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                    TrieNode node)
{
   Bool *isSet = (Bool *)visitor->_data;
   uint8 byte, bit;
   ASSERT(fromOffset == toOffset);
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   if ((*isSet = node->_bitmap[byte] & (1u << bit)) == FALSE) {
      node->_bitmap[byte] |= (1u << bit);
   }
   return TRIE_VISITOR_RET_END;
}


/** 
 * A visitor to set bits in range
 */

static inline TrieVisitorReturnCode
SetBitsVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                     uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                     TrieNode node)
{
   uint8 byte, bit;
   uint8 toByte, toBit;
   if (visitor->_stat != NULL && IS_TRIE_STAT_FLAG_BITSET_ON(visitor->_stat->_flag)) {
      visitor->_stat->_totalSet +=
         (toOffset-fromOffset+1) -
         TrieGetSetBitsInLeaf(node, fromOffset, toOffset);
   }
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   if (toOffset == fromOffset) {
      // fast pass for special case
      node->_bitmap[byte] |= (1u << bit);
      return TRIE_VISITOR_RET_CONT;
   }
   GET_BITMAP_BYTE_BIT(toOffset, toByte, toBit);
   // fill bits in the middle bytes
   if (toByte - byte > 1) {
      memset(&node->_bitmap[byte+1], (int)-1, toByte - byte - 1);
   }
   // fill the bits greater and equal to the bit
   node->_bitmap[byte] |= ~((1u<<bit)-1);
   // fill the bits smaller and equal to the max bit
   node->_bitmap[toByte] |= ~((~((1u<<toBit)-1)) << 1);
   return TRIE_VISITOR_RET_CONT;
}


/**
 * A visitor to traverse bit
 */

static inline TrieVisitorReturnCode
TraverseVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                      TrieNode node)
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

   bitmap = (uint8 *)node->_bitmap;
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


/**
 *  A vistor to update statistics
 */

static inline TrieVisitorReturnCode
UpdateStatVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 nodeAddr, uint8 height, TrieNode node)
{
   if (IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(visitor->_stat->_flag)) {
      visitor->_stat->_memoryInUse += sizeof(*node);
   }
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
UpdateStatVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                        uint64 nodeAddr, uint16 fromOffset,
                        uint16 toOffset, TrieNode node)
{
   TrieStatistics *stat = visitor->_stat;
   if (IS_TRIE_STAT_FLAG_BITSET_ON(stat->_flag)) {
      stat->_totalSet += TrieGetSetBitsInLeaf(node, fromOffset, toOffset);
   }
   if (IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(stat->_flag)) {
      stat->_memoryInUse += sizeof(*node);
   }
   if (IS_TRIE_STAT_FLAG_COUNT_LEAF_ON(stat->_flag)) {
      stat->_leafCount++;
   }
   return TRIE_VISITOR_RET_CONT;
}


/**
 *  A vistor to delete trie
 */

static inline TrieVisitorReturnCode
DeleteInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                uint64 nodeAddr, uint8 height, TrieNode node)
{
   FreeTrieNode(node, visitor->_stat);
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
DeleteLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
               uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
               TrieNode node)
{
   FreeTrieNode(node, visitor->_stat);
   return TRIE_VISITOR_RET_CONT;
}


/**
 *  A visitor to deserialize
 */

static inline TrieVisitorReturnCode
DeserializeVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 nodeAddr, uint8 height, TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData *)visitor->_data;
   const BlockTrackingSparseBitmapStream *stream =
      (const BlockTrackingSparseBitmapStream*)data->_cb;
   const BlockTrackingSparseBitmapStream *streamEnd =
      (const BlockTrackingSparseBitmapStream*)data->_cbData;
   uint64 targetNodeAddr;
   uint64 maxNodeAddr;

   if (stream == streamEnd || stream->_nodeOffset == -1) {
      return TRIE_VISITOR_RET_END;
   }

   targetNodeAddr = stream->_nodeOffset << ADDR_BITS_IN_LEAF;
   maxNodeAddr = nodeAddr | NODE_VALUE_MASK(height+1);

   ASSERT(targetNodeAddr >= nodeAddr);
   if (targetNodeAddr > maxNodeAddr) {
      return TRIE_VISITOR_RET_SKIP_CHILDREN;
   }
   if (*pNode == NULL) {
      if ((*pNode = AllocateTrieNode(visitor->_stat, height == 0)) == NULL) {
         return TRIE_VISITOR_RET_OUT_OF_MEM;
      }
   } else if (height == 0) {
      TrieLeafNodeMergeFlatBitmap(*pNode, (const char *)stream->_node._bitmap,
                                  visitor->_stat);
      // to the next stream
      ++stream;
      data->_cb = (void *)stream;
   }
   return TRIE_VISITOR_RET_CONT;
}

static inline TrieVisitorReturnCode
DeserializeVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                          uint64 nodeAddr, uint8 height, TrieNode node)
{
   return DeserializeVisitNullNode(visitor, nodeAddr, height, &node);
}

static inline TrieVisitorReturnCode
DeserializeVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 nodeAddr,
                         uint16 fromOffset, uint16 toOffset,
                         TrieNode node)
{
   return DeserializeVisitNullNode(visitor, nodeAddr, 0, &node);
}


/**
 *  A vistor to serialize
 */

static inline TrieVisitorReturnCode
SerializeVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                       uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                       TrieNode node)
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
      memcpy(stream->_node._bitmap, node->_bitmap, sizeof(*node));
      // to the next stream
      ++stream;
      data->_cb = (void *)stream;
      return TRIE_VISITOR_RET_CONT;
   }
   return TRIE_VISITOR_RET_OVERFLOW;
}


/**
 *  A visitor to count leaf
 */

static inline TrieVisitorReturnCode
CountLeafVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                       uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                       TrieNode node)
{
   uint16 *count = (uint16 *)visitor->_data;
   ++(*count);
   return TRIE_VISITOR_RET_CONT;
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

CBTBitmapError
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
   TrieVisitorReturnCode trieRetCode = TRIE_VISITOR_RET_ABORT;
   if (!TrieIndexValidation(i)) {
      return CBT_BMAP_ERR_INVALID_ADDR;
   }

   for (; i < MAX_NUM_TRIES && fromAddr <= toAddr ; ++i) {
      trieRetCode =
         TrieAccept(&bitmap->_tries[i], i, &fromAddr, toAddr, visitor);
      if (trieRetCode != TRIE_VISITOR_RET_CONT &&
          trieRetCode != TRIE_VISITOR_RET_SKIP_CHILDREN) {
         break;
      }
   }
   if ((i == MAX_NUM_TRIES || fromAddr > toAddr) &&
       (trieRetCode == TRIE_VISITOR_RET_SKIP_CHILDREN ||
        trieRetCode == TRIE_VISITOR_RET_CONT)) {
      trieRetCode = TRIE_VISITOR_RET_END;
   }
   return BlockTrackingSparseBitmapTranslateTrieRetCode(trieRetCode);
}


/*
 *-----------------------------------------------------------------------------
 *
 * BlockTrackingSparseBitmapDeleteT_tries --
 *
 *    Delete all t_tries in the bitmap.
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
BlockTrackingSparseBitmapDeleteT_tries(CBTBitmap bitmap)
{
   CBTBitmapError ret;
   BlockTrackingSparseBitmapVisitor deleteTrie =
      {DeleteLeafNode, NULL, DeleteInnerNode, NULL, NULL,
       (IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(bitmap->_stat._flag)) ? &bitmap->_stat : NULL
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
   BlockTrackingSparseBitmapVisitor setBit =
      {SetBitVisitLeafNode, NULL, NULL,
       AllocateNodeVisitNullNode, &isSet,
       (TRIE_STAT_FLAG_IS_NULL(bitmap->_stat._flag)) ? NULL : &bitmap->_stat
      };

   ret = BlockTrackingSparseBitmapAccept(bitmap, addr, addr, &setBit);

   if (ret == CBT_BMAP_ERR_OK) {
      if (IS_TRIE_STAT_FLAG_BITSET_ON(bitmap->_stat._flag) && !isSet) {
         bitmap->_stat._totalSet++;
      }
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
   BlockTrackingSparseBitmapVisitor setBits =
      {SetBitsVisitLeafNode, NULL, NULL, AllocateNodeVisitNullNode, NULL,
       (TRIE_STAT_FLAG_IS_NULL(bitmap->_stat._flag)) ? NULL : &bitmap->_stat
      };

   return BlockTrackingSparseBitmapAccept(bitmap, fromAddr, toAddr, &setBits);
}

static CBTBitmapError
BlockTrackingSparseBitmapQueryBit(CBTBitmap bitmap,
                                  uint64 addr, Bool *isSetBefore)
{
   BlockTrackingSparseBitmapVisitor queryBit =
      {QueryBitVisitLeafNode, NULL, NULL,
       QueryBitVisitNullNode, isSetBefore, NULL
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
   BlockTrackingSparseBitmapVisitor traverse =
      {TraverseVisitLeafNode, NULL, NULL, NULL, &data, NULL};
   return BlockTrackingSparseBitmapAccept(bitmap, fromAddr, toAddr, &traverse);
}


/*
 * The callback data for traverse extents.
 */

typedef struct {
   uint64 _start;
   uint64 _end;
   uint64 _extStart;
   uint64 _extEnd;
   CBTBitmapAccessExtentCB _extHdl;
   void *_extHdlData;
   Bool _extHdlRet;
} GetExtentsData;


/*
 * The callback of traverse extents.
 */

static Bool
GetExtents(void *data, uint64 addr)
{
   GetExtentsData *extData = (GetExtentsData *)data;
   if (addr < extData->_start) {
      return TRUE; // continue
   } else if (addr > extData->_end) {
      if (!extData->_extHdl(extData->_extHdlData,
                            extData->_extStart, extData->_extEnd)) {
         extData->_extHdlRet = FALSE;
      }
      extData->_extStart = extData->_extEnd = -1;
      return FALSE;
   } else {
      if (extData->_extStart == -1) {
         // start of the extent
         extData->_extStart = addr;
         extData->_extEnd = addr;
      } else if (addr == extData->_extEnd + 1) {
         // advance the end of extent
         extData->_extEnd = addr;
      } else {
         // end of the extent
         if (!extData->_extHdl(extData->_extHdlData,
                               extData->_extStart, extData->_extEnd)) {
            extData->_extHdlRet = FALSE;
            return FALSE;
         }
         extData->_extStart = extData->_extEnd = addr;
      }
   }
   return TRUE;
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
   GetExtentsData extData = {fromAddr, toAddr, -1, -1, cb, cbData, TRUE};
   BlockTrackingSparseBitmapTraverseByBit(bitmap, fromAddr, toAddr,
                                          GetExtents, &extData);
   if (!extData._extHdlRet) {
      return CBT_BMAP_ERR_FAIL;
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
   BlockTrackingSparseBitmapVisitor updateStat =
      {UpdateStatVisitLeafNode, UpdateStatVisitInnerNode, NULL, NULL, NULL,
       &bitmap->_stat};
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
   BlockTrackingSparseBitmapVisitor deserialize =
      {DeserializeVisitLeafNode, DeserializeVisitInnerNode, NULL,
       DeserializeVisitNullNode, &data,
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
   BlockTrackingSparseBitmapVisitor serialize =
      {SerializeVisitLeafNode, NULL, NULL, NULL, &data, NULL};
   ret = BlockTrackingSparseBitmapAccept(bitmap, 0, -1, &serialize);
   // append a terminator as the end of stream if the stream is not exhausted
   if (ret == CBT_BMAP_ERR_OK) {
      begin = (BlockTrackingSparseBitmapStream *)data._cb;
      if (begin != end) {
         begin->_nodeOffset = -1;
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
      flag |= TRIE_STAT_FLAG_COUNT_LEAF;
   }
   if (mode & CBT_BMAP_MODE_FAST_STATISTIC) {
      flag |= TRIE_STAT_FLAG_BITSET;
      flag |= TRIE_STAT_FLAG_MEMORY_ALLOC;
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
      BlockTrackingSparseBitmapDeleteT_tries(bitmap);
      FreeBitmap(bitmap);
   }
}

CBTBitmapError
CBTBitmap_SetAt(CBTBitmap bitmap, uint64 addr, Bool *oldValue)
{
   if (bitmap == NULL) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }

   return BlockTrackingSparseBitmapSetBit(bitmap, addr, oldValue);
}

CBTBitmapError
CBTBitmap_SetInRange(CBTBitmap bitmap, uint64 fromAddr, uint64 toAddr)
{
   if (bitmap == NULL || toAddr < fromAddr) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }

   return BlockTrackingSparseBitmapSetBits(bitmap, fromAddr, toAddr);
}

CBTBitmapError
CBTBitmap_IsSet(CBTBitmap bitmap, uint64 addr, Bool *isSet)
{
   if (bitmap == NULL || isSet == NULL) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }
   return BlockTrackingSparseBitmapQueryBit(bitmap, addr, isSet);
}

CBTBitmapError
CBTBitmap_TraverseByBit(CBTBitmap bitmap, uint64 fromAddr, uint64 toAddr,
                        CBTBitmapAccessBitCB cb, void *cbData)
{
   if (bitmap == NULL || cb == NULL || toAddr < fromAddr) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }
   return BlockTrackingSparseBitmapTraverseByBit(bitmap, fromAddr, toAddr,
                                                 cb, cbData);
}

CBTBitmapError
CBTBitmap_TraverseByExtent(CBTBitmap bitmap, uint64 fromAddr, uint64 toAddr,
                           CBTBitmapAccessExtentCB cb, void *cbData)
{
   if (bitmap == NULL || cb == NULL || toAddr < fromAddr) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }
   return BlockTrackingSparseBitmapTraverseByExtent(bitmap, fromAddr, toAddr,
                                                    cb, cbData);
}

CBTBitmapError
CBTBitmap_Swap(CBTBitmap bitmap1, CBTBitmap bitmap2)
{
   struct CBTBitmap tmp;
   if (bitmap1 == NULL || bitmap2 == NULL) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }
   if (bitmap1 != bitmap2) {
      memcpy(&tmp, bitmap1, sizeof(tmp));
      memcpy(bitmap1, bitmap2, sizeof(tmp));
      memcpy(bitmap2, &tmp, sizeof(tmp));
   }
   return CBT_BMAP_ERR_OK;
}

CBTBitmapError
CBTBitmap_Merge(CBTBitmap dest, CBTBitmap src)
{
   uint8 i;
   TrieStatistics *stat;
   TrieVisitorReturnCode trieRetCode = TRIE_VISITOR_RET_CONT;

   if (dest == NULL || src == NULL) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }

   stat = (TRIE_STAT_FLAG_IS_NULL(dest->_stat._flag)) ? NULL : &dest->_stat;
   for (i = 0; i < MAX_NUM_TRIES ; ++i) {
      trieRetCode = TrieMerge(&dest->_tries[i], src->_tries[i], i, stat);
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
   if (bitmap == NULL || stream == NULL || streamLen == 0) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }

   return BlockTrackingSparseBitmapDeserialize(bitmap, stream, streamLen);
}

CBTBitmapError
CBTBitmap_Serialize(CBTBitmap bitmap, char *stream, uint32 streamLen)
{
   if (bitmap == NULL || stream == NULL || streamLen == 0) {
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

CBTBitmapError
CBTBitmap_GetStreamSize(CBTBitmap bitmap, uint32 *streamLen)
{
   CBTBitmapError ret = CBT_BMAP_ERR_OK;
   uint16 leafCount;
   if (bitmap == NULL || streamLen == NULL) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }
   if (!IS_TRIE_STAT_FLAG_COUNT_LEAF_ON(bitmap->_stat._flag)) {
      TrieStatistics stat = bitmap->_stat;
      memset(&bitmap->_stat, 0, sizeof(bitmap->_stat));
      bitmap->_stat._flag |= TRIE_STAT_FLAG_COUNT_LEAF;
      BlockTrackingSparseBitmapUpdateStatistics(bitmap);
      leafCount = bitmap->_stat._leafCount;
      bitmap->_stat = stat;
   } else {
      leafCount = bitmap->_stat._leafCount;
   }
   ASSERT(leafCount <= MAX_NUM_LEAVES);
   ++leafCount; // need a terminator
   *streamLen = leafCount * sizeof(BlockTrackingSparseBitmapStream);
   return ret;
}

CBTBitmapError
CBTBitmap_GetBitCount(CBTBitmap bitmap, uint32 *bitCount)
{
   if (bitmap == NULL || bitCount == 0) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }
   if (!IS_TRIE_STAT_FLAG_BITSET_ON(bitmap->_stat._flag)) {
      TrieStatistics stat = bitmap->_stat;
      memset(&bitmap->_stat, 0, sizeof(bitmap->_stat));
      bitmap->_stat._flag |= TRIE_STAT_FLAG_BITSET;
      BlockTrackingSparseBitmapUpdateStatistics(bitmap);
      *bitCount = bitmap->_stat._totalSet;
      bitmap->_stat = stat;
   } else {
      *bitCount = bitmap->_stat._totalSet;
   }
   return CBT_BMAP_ERR_OK;
}

CBTBitmapError
CBTBitmap_GetMemoryInUse(CBTBitmap bitmap, uint32 *memoryInUse)
{
   if (bitmap == NULL || memoryInUse == 0) {
      return CBT_BMAP_ERR_INVALID_ARG;
   }
   if (!IS_TRIE_STAT_FLAG_MEMORY_ALLOC_ON(bitmap->_stat._flag)) {
      TrieStatistics stat = bitmap->_stat;
      memset(&bitmap->_stat, 0, sizeof(bitmap->_stat));
      bitmap->_stat._flag |= TRIE_STAT_FLAG_MEMORY_ALLOC;
      BlockTrackingSparseBitmapUpdateStatistics(bitmap);
      *memoryInUse = bitmap->_stat._memoryInUse;
      bitmap->_stat = stat;
   } else {
      *memoryInUse = bitmap->_stat._memoryInUse;
   }
   return CBT_BMAP_ERR_OK;
}

uint64
CBTBitmap_GetCapacity()
{
   return MAX_NUM_LEAVES * (1ul << ADDR_BITS_IN_LEAF);
}
