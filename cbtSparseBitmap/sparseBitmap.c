#include "cbtSparseBitmap.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define MAX_NUM_TRIES 6
#define ADDR_BITS_IN_LEAF 9
#define ADDR_BITS_IN_INNER_NODE 3
#define ADDR_BITS_IN_HEIGHT(h) (ADDR_BITS_IN_LEAF+(h-1)*ADDR_BITS_IN_INNER_NODE)
#define NUM_TRIE_WAYS (1u << ADDR_BITS_IN_INNER_NODE)
#define TRIE_WAY_MASK ((uint64_t)NUM_TRIE_WAYS-1)
#define MAX_NUM_LEAVES (1u << ((MAX_NUM_TRIES-1)*ADDR_BITS_IN_INNER_NODE))

#define COUNT_SET_BITS(x, c) \
   while((x) != 0) {          \
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


typedef union TrieNode_t {
   union TrieNode_t *_children[NUM_TRIE_WAYS];
   char _bitmap[NUM_TRIE_WAYS * sizeof(union TrieNode_t*)];
} *TrieNode;

#define LEAF_VALUE_MASK ((uint64_t)sizeof(union TrieNode_t) * 8 - 1)
#define NODE_ADDR_MASK(h)        \
   ((h==0) ? ~LEAF_VALUE_MASK :  \
      ~(((uint64_t)1ull << ADDR_BITS_IN_HEIGHT(h)) - 1))
#define NODE_VALUE_MASK(h)  (~NODE_ADDR_MASK(h))

#define GET_NODE_WAYS(addr, h) ((addr>>ADDR_BITS_IN_HEIGHT(h)) & TRIE_WAY_MASK)

#define TRIE_STAT_FLAG_SET   1
#define TRIE_STAT_FLAG_MEM  (1 << 1)
#define TRIE_STAT_FLAG_LEAF (1 << 2)

#define TRIE_STAT_FLAG_IS_NULL(f) ((f) == 0)
#define TRIE_STAT_FLAG_IS_SET(f) ((f) & TRIE_STAT_FLAG_SET)
#define TRIE_STAT_FLAG_IS_MEM(f) ((f) & TRIE_STAT_FLAG_MEM)
#define TRIE_STAT_FLAG_IS_LEAF(f) ((f) & TRIE_STAT_FLAG_LEAF)

typedef struct TrieStatistics_t {
   uint32_t _totalSet;
   uint32_t _memoryInUse;
   uint16_t _leafCount;
   uint16_t _flag;
} TrieStatistics;

struct BlockTrackingSparseBitmap_t {
   TrieNode _tries[MAX_NUM_TRIES];
   TrieStatistics _stat;
   uint32_t _padding;
};

typedef struct BlockTrackingBitmapCallbackData_t {
   void *_cb;
   void *_cbData;
} BlockTrackingBitmapCallbackData;


// pack it to avoid waste bytes for allignment
typedef struct BlockTrackingSparseBitmapStream_t {
   uint16_t _nodeOffset;
   union TrieNode_t _node;
} __attribute__((packed)) BlockTrackingSparseBitmapStream;

// visitor pattern
typedef enum {
   TRIE_VISITOR_RET_CONT = 0,
   TRIE_VISITOR_RET_END,
   TRIE_VISITOR_RET_SKIP_CHILDREN,
   TRIE_VISITOR_RET_OUT_OF_MEM,
   TRIE_VISITOR_RET_OVERFLOW,
   TRIE_VISITOR_RET_ABORT,
} TrieVisitorReturnCode;

struct BlockTrackingSparseBitmapVisitor_t;
typedef TrieVisitorReturnCode (*VisitInnerNode) (
      struct BlockTrackingSparseBitmapVisitor_t *visitor,
      uint64_t nodeAddr, uint8_t height, TrieNode node);

typedef TrieVisitorReturnCode (*VisitLeafNode) (
      struct BlockTrackingSparseBitmapVisitor_t *visitor,
      uint64_t nodeAddr, uint16_t fromOffset, uint16_t toOffset, TrieNode node);

typedef TrieVisitorReturnCode (*VisitNullNode) (
      struct BlockTrackingSparseBitmapVisitor_t *visitor, uint64_t nodeAddr,
      uint8_t height, TrieNode *pNode);

typedef struct BlockTrackingSparseBitmapVisitor_t {
   VisitLeafNode _visitLeafNode;
   VisitInnerNode _beforeVisitInnerNode;
   VisitInnerNode _afterVisitInnerNode;
   VisitNullNode _visitNullNode;
   void *_data;
   TrieStatistics *_stat;
} BlockTrackingSparseBitmapVisitor;

////////////////////////////////////////////////////////////////////////////////
//   Allocate/Free Functions
////////////////////////////////////////////////////////////////////////////////

static TrieNode
AllocateTrieNode(TrieStatistics *stat, Bool isLeaf)
{
   TrieNode node = (TrieNode)calloc(1, sizeof(*node));
   if (node != NULL && stat != NULL) {
      if (TRIE_STAT_FLAG_IS_MEM(stat->_flag) && node != NULL) {
         stat->_memoryInUse += sizeof(*node);
      }
      if (isLeaf && TRIE_STAT_FLAG_IS_LEAF(stat->_flag)) {
         stat->_leafCount++;
      }
   }
   return node;
}

static void
FreeTrieNode(TrieNode node, TrieStatistics *stat)
{
   if (stat != NULL && TRIE_STAT_FLAG_IS_MEM(stat->_flag)) {
      stat->_memoryInUse -= sizeof(*node);
   }
   free(node);
}

static BlockTrackingSparseBitmap
AllocateBitmap()
{
   BlockTrackingSparseBitmap bitmap =
      (BlockTrackingSparseBitmap)calloc(1, sizeof(*bitmap));
   return bitmap;
}

static void
FreeBitmap(BlockTrackingSparseBitmap bitmap)
{
   free(bitmap);
}

////////////////////////////////////////////////////////////////////////////////
//   Trie Functions
////////////////////////////////////////////////////////////////////////////////

static uint16_t
TrieGetSetBitsInLeaf(TrieNode leaf, uint16_t fromOffset, uint16_t toOffset)
{
   uint64_t *bitmap = (uint64_t *)leaf->_bitmap;
   uint8_t i;
   uint8_t byte, bit;
   uint8_t toByte, toBit;
   uint16_t cnt = 0;
   GET_BITMAP_BYTE8_BIT(fromOffset, byte, bit);
   GET_BITMAP_BYTE8_BIT(toOffset, toByte, toBit);
   for (i = byte ; i <= toByte ; ++i) {
      uint64_t c = bitmap[i];
      if (i == byte) {
         c &= ~((1ull << bit) - 1);
      }
      if (i == toByte) {
         c &= ~((~((1ull << toBit) - 1)) << 1);
      }
      COUNT_SET_BITS(c, cnt);
   }
   return cnt;
}

static void
TrieLeafNodeMergeFlatBitmap(TrieNode destNode, const char *flatBitmap,
                            TrieStatistics *stat)
{
   uint8_t i;
   uint64_t *destBitmap = (uint64_t*)destNode->_bitmap;
   const uint64_t *srcBitmap = (const uint64_t*)flatBitmap;
   if (stat != NULL && TRIE_STAT_FLAG_IS_SET(stat->_flag)) {
      for (i = 0 ; i < sizeof(*destNode)/sizeof(uint64_t) ; ++i) {
         uint64_t o = destBitmap[i];
         uint64_t n = o | srcBitmap[i];
         o ^= n;
         COUNT_SET_BITS(o, stat->_totalSet);
         destBitmap[i] = n;
      }
   } else {
      for (i = 0 ; i < sizeof(*destNode)/sizeof(uint64_t) ; ++i) {
         destBitmap[i] |= srcBitmap[i];
      }
   }
}

static TrieVisitorReturnCode
TrieMerge(TrieNode *pDestNode, TrieNode srcNode, uint8_t height,
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
      uint8_t way;
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

static TrieVisitorReturnCode
TrieAccept(TrieNode *pNode, uint8_t height, uint64_t *fromAddr, uint64_t toAddr,
           BlockTrackingSparseBitmapVisitor *visitor)
{
   TrieVisitorReturnCode ret = TRIE_VISITOR_RET_CONT;
   uint64_t nodeAddr;
   assert(*fromAddr <= toAddr);
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
      uint16_t fromOffset = *fromAddr & LEAF_VALUE_MASK;
      uint16_t toOffset =
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
      uint8_t way;
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

static uint8_t
TrieMaxHeight(uint64_t addr)
{
   uint8_t height = 0;
   addr >>= ADDR_BITS_IN_LEAF;
   while (addr != 0) {
      addr >>= ADDR_BITS_IN_INNER_NODE;
      ++height;
   }
   return height;
}

static inline Bool
TrieIndexValidation(uint8_t trieIndex)
{
   return trieIndex < MAX_NUM_TRIES;
}


////////////////////////////////////////////////////////////////////////////////
//   Visitors
////////////////////////////////////////////////////////////////////////////////

/**
 * A visitor to create NULL node
 */

static TrieVisitorReturnCode
AllocateNodeVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                          uint64_t nodeAddr, uint8_t height,
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

static TrieVisitorReturnCode
QueryBitVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64_t nodeAddr, uint8_t height,
                      TrieNode *pNode)
{
   Bool *isSet = (Bool *)visitor->_data;
   *isSet = FALSE;
   return TRIE_VISITOR_RET_END;
}

static TrieVisitorReturnCode
QueryBitVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64_t nodeAddr, uint16_t fromOffset, uint16_t toOffset,
                      TrieNode node)
{
   Bool *isSet = (Bool *)visitor->_data;
   uint8_t byte, bit;
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   *isSet = node->_bitmap[byte] & (1u << bit);
   return TRIE_VISITOR_RET_END;
}

/**
 * A visitor to set bit
 */

static TrieVisitorReturnCode
SetBitVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                    uint64_t nodeAddr, uint16_t fromOffset, uint16_t toOffset,
                    TrieNode node)
{
   Bool *isSet = (Bool *)visitor->_data;
   uint8_t byte, bit;
   assert(fromOffset == toOffset);
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   *isSet = node->_bitmap[byte] & (1u << bit);
   node->_bitmap[byte] |= (1u << bit);
   return TRIE_VISITOR_RET_END;
}

/**
 * A visitor to set bits in range
 */

static TrieVisitorReturnCode
SetBitsVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                     uint64_t nodeAddr, uint16_t fromOffset, uint16_t toOffset,
                     TrieNode node)
{
   uint8_t byte, bit;
   uint8_t toByte, toBit;
   if (visitor->_stat != NULL && TRIE_STAT_FLAG_IS_SET(visitor->_stat->_flag)) {
      visitor->_stat->_totalSet +=
         (toOffset-fromOffset+1) -
         TrieGetSetBitsInLeaf(node, fromOffset, toOffset);
   }
   if (toOffset == fromOffset) {
      // fast pass for special case
      node->_bitmap[byte] |= (1u << bit);
      return TRIE_VISITOR_RET_CONT;
   }
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
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

static TrieVisitorReturnCode
TraverseVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64_t nodeAddr, uint16_t fromOffset, uint16_t toOffset,
                      TrieNode node)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData*)visitor->_data;
   BlockTrackingSparseBitmapAccessBitCB cb =
      (BlockTrackingSparseBitmapAccessBitCB)data->_cb;

   uint8_t i;
   uint8_t byte, bit;
   uint8_t toByte, toBit;
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   GET_BITMAP_BYTE_BIT(toOffset, toByte, toBit);

   uint8_t *bitmap = node->_bitmap;
   for (i = byte, nodeAddr += i * 8 ; i <= toByte ; ++i, nodeAddr += 8) {
      uint8_t c = bitmap[i];
      uint b = (i == byte) ? bit : 0;
      uint e = (i == toByte) ? toBit : 7;
      c >>= b;
      while (c > 0 && b <= e) {
         if (c & 0x1 > 0) {
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

static TrieVisitorReturnCode
UpdateStatVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64_t nodeAddr, uint8_t height, TrieNode node)
{
   if (TRIE_STAT_FLAG_IS_MEM(visitor->_stat->_flag)) {
      visitor->_stat->_memoryInUse += sizeof(*node);
   }
   return TRIE_VISITOR_RET_CONT;
}

static TrieVisitorReturnCode
UpdateStatVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                        uint64_t nodeAddr, uint16_t fromOffset,
                        uint16_t toOffset, TrieNode node)
{
   TrieStatistics *stat = visitor->_stat;
   if (TRIE_STAT_FLAG_IS_SET(stat->_flag)) {
      stat->_totalSet += TrieGetSetBitsInLeaf(node, fromOffset, toOffset);
   }
   if (TRIE_STAT_FLAG_IS_MEM(stat->_flag)) {
      stat->_memoryInUse += sizeof(*node);
   }
   if (TRIE_STAT_FLAG_IS_LEAF(stat->_flag)) {
      stat->_leafCount++;
   }
   return TRIE_VISITOR_RET_CONT;
}

/**
 *  A vistor to delete trie
 */

static TrieVisitorReturnCode
DeleteInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                uint64_t nodeAddr, uint8_t height, TrieNode node)
{
   FreeTrieNode(node, visitor->_stat);
   return TRIE_VISITOR_RET_CONT;
}

static TrieVisitorReturnCode
DeleteLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
               uint64_t nodeAddr, uint16_t fromOffset, uint16_t toOffset,
               TrieNode node)
{
   FreeTrieNode(node, visitor->_stat);
   return TRIE_VISITOR_RET_CONT;
}


/**
 *  A visitor to deserialize
 */

static TrieVisitorReturnCode
DeserializeVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64_t nodeAddr, uint8_t height, TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData *)visitor->_data;
   const BlockTrackingSparseBitmapStream *stream =
      (const BlockTrackingSparseBitmapStream*)data->_cb;
   const BlockTrackingSparseBitmapStream *streamEnd =
      (const BlockTrackingSparseBitmapStream*)data->_cbData;
   uint64_t targetNodeAddr;
   uint64_t maxNodeAddr;

   if (stream == streamEnd || stream->_nodeOffset == -1) {
      return TRIE_VISITOR_RET_END;
   }

   targetNodeAddr = stream->_nodeOffset << ADDR_BITS_IN_LEAF;
   maxNodeAddr = nodeAddr | NODE_VALUE_MASK(height+1);

   assert(targetNodeAddr >= nodeAddr);
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

static TrieVisitorReturnCode
DeserializeVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                          uint64_t nodeAddr, uint8_t height, TrieNode node)
{
   return DeserializeVisitNullNode(visitor, nodeAddr, height, &node);
}

static TrieVisitorReturnCode
DeserializeVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64_t nodeAddr,
                         uint16_t fromOffset, uint16_t toOffset,
                         TrieNode node)
{
   return DeserializeVisitNullNode(visitor, nodeAddr, 0, &node);
}

/**
 *  A vistor to serialize
 */

static TrieVisitorReturnCode
SerializeVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                       uint64_t nodeAddr, uint16_t fromOffset, uint16_t toOffset,
                       TrieNode node)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData *)visitor->_data;
   BlockTrackingSparseBitmapStream *stream =
      (BlockTrackingSparseBitmapStream *)data->_cb;
   BlockTrackingSparseBitmapStream *streamEnd =
      (BlockTrackingSparseBitmapStream *)data->_cbData;
   uint16_t nodeOffset = nodeAddr >> ADDR_BITS_IN_LEAF;

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
static TrieVisitorReturnCode
CountLeafVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                       uint64_t nodeAddr, uint16_t fromOffset, uint16_t toOffset,
                       TrieNode node)
{
   uint16_t *count = (uint16_t *)visitor->_data;
   ++(*count);
   return TRIE_VISITOR_RET_CONT;
}

////////////////////////////////////////////////////////////////////////////////
//   BlockTrackingSparseBitmap Internal Functions
////////////////////////////////////////////////////////////////////////////////

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmapTranslateTrieRetCode(TrieVisitorReturnCode trieRetCode)
{
   BlockTrackingSparseBitmapError ret = BLOCKTRACKING_BMAP_ERR_FAIL;
   switch (trieRetCode) {
      case TRIE_VISITOR_RET_ABORT:
      case TRIE_VISITOR_RET_SKIP_CHILDREN:
      case TRIE_VISITOR_RET_CONT:
         ret = BLOCKTRACKING_BMAP_ERR_FAIL;
         break;
      case TRIE_VISITOR_RET_END:
         ret = BLOCKTRACKING_BMAP_ERR_OK;
         break;
      case TRIE_VISITOR_RET_OUT_OF_MEM:
         ret = BLOCKTRACKING_BMAP_ERR_OUT_OF_MEM;
         break;
      case TRIE_VISITOR_RET_OVERFLOW:
         ret = BLOCKTRACKING_BMAP_ERR_OUT_OF_RANGE;
         break;
      default:
         break;
   }
   return ret;
}

/**
 * accept a visitor
 */

static BlockTrackingSparseBitmapError
BlockTrackingSparseBitmapAccept(BlockTrackingSparseBitmap bitmap,
                                uint64_t fromAddr, uint64_t toAddr,
                                BlockTrackingSparseBitmapVisitor *visitor)
{
   uint8_t i = TrieMaxHeight(fromAddr);
   TrieVisitorReturnCode trieRetCode;
   if (!TrieIndexValidation(i)) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ADDR;
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

static BlockTrackingSparseBitmapError
BlockTrackingSparseBitmapDeleteTries(BlockTrackingSparseBitmap bitmap)
{
   BlockTrackingSparseBitmapError ret;
   BlockTrackingSparseBitmapVisitor deleteTrie =
      {DeleteLeafNode, NULL, DeleteInnerNode, NULL, NULL,
       (TRIE_STAT_FLAG_IS_MEM(bitmap->_stat._flag)) ? &bitmap->_stat : NULL
      };
   ret = BlockTrackingSparseBitmapAccept(bitmap, 0, -1, &deleteTrie);
   if (TRIE_STAT_FLAG_IS_MEM(bitmap->_stat._flag)) {
      assert(bitmap->_stat._memoryInUse == sizeof(*bitmap));
   }
   return ret;
}

static BlockTrackingSparseBitmapError
BlockTrackingSparseBitmapSetBit(BlockTrackingSparseBitmap bitmap, uint64_t addr,
                                Bool *isSetBefore)
{
   BlockTrackingSparseBitmapError ret;
   Bool isSet = FALSE;
   BlockTrackingSparseBitmapVisitor setBit =
      {SetBitVisitLeafNode, NULL, NULL,
       AllocateNodeVisitNullNode, &isSet,
       (TRIE_STAT_FLAG_IS_NULL(bitmap->_stat._flag)) ? NULL : &bitmap->_stat
      };

   ret = BlockTrackingSparseBitmapAccept(bitmap, addr, addr, &setBit);

   if (ret == BLOCKTRACKING_BMAP_ERR_OK) {
      if (TRIE_STAT_FLAG_IS_SET(bitmap->_stat._flag) && !isSet) {
         bitmap->_stat._totalSet++;
      }
      if (isSetBefore != NULL) {
         *isSetBefore = isSet;
      }
   }
   return ret;
}

static BlockTrackingSparseBitmapError
BlockTrackingSparseBitmapSetBits(BlockTrackingSparseBitmap bitmap,
                                 uint64_t fromAddr, uint64_t toAddr)
{
   BlockTrackingSparseBitmapVisitor setBits =
      {SetBitsVisitLeafNode, NULL, NULL, AllocateNodeVisitNullNode, NULL,
       (TRIE_STAT_FLAG_IS_NULL(bitmap->_stat._flag)) ? NULL : &bitmap->_stat
      };

   return BlockTrackingSparseBitmapAccept(bitmap, fromAddr, toAddr, &setBits);
}

static BlockTrackingSparseBitmapError
BlockTrackingSparseBitmapQueryBit(BlockTrackingSparseBitmap bitmap,
                                  uint64_t addr, Bool *isSetBefore)
{
   BlockTrackingSparseBitmapVisitor queryBit =
      {QueryBitVisitLeafNode, NULL, NULL,
       QueryBitVisitNullNode, isSetBefore, NULL
      };
   return BlockTrackingSparseBitmapAccept(bitmap, addr, addr, &queryBit);
}


static BlockTrackingSparseBitmapError
BlockTrackingSparseBitmapTraverseByBit(BlockTrackingSparseBitmap bitmap,
                                       uint64_t fromAddr, uint64_t toAddr,
                                       BlockTrackingSparseBitmapAccessBitCB cb,
                                       void *cbData)
{
   BlockTrackingBitmapCallbackData data = {cb, cbData};
   BlockTrackingSparseBitmapVisitor traverse =
      {TraverseVisitLeafNode, NULL, NULL, NULL, &data, NULL};
   return BlockTrackingSparseBitmapAccept(bitmap, fromAddr, toAddr, &traverse);
}

typedef struct {
   uint64_t _start;
   uint64_t _end;
   uint64_t _extStart;
   uint64_t _extEnd;
   BlockTrackingSparseBitmapAccessExtentCB _extHdl;
   void *_extHdlData;
   Bool _extHdlRet;
} GetExtentsData;

static Bool
GetExtents(void *data, uint64_t addr)
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
         // star of the extent
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

static BlockTrackingSparseBitmapError
BlockTrackingSparseBitmapTraverseByExtent(
                                    BlockTrackingSparseBitmap bitmap,
                                    uint64_t fromAddr, uint64_t toAddr,
                                    BlockTrackingSparseBitmapAccessExtentCB cb,
                                    void *cbData)
{
   GetExtentsData extData = {fromAddr, toAddr, -1, -1, cb, cbData, TRUE};
   BlockTrackingSparseBitmapTraverseByBit(bitmap, fromAddr, toAddr,
                                          GetExtents, &extData);
   if (!extData._extHdlRet) {
      return BLOCKTRACKING_BMAP_ERR_FAIL;
   }
   if (extData._extStart != -1) {
      if (!cb(cbData, extData._extStart, extData._extEnd)) {
         return BLOCKTRACKING_BMAP_ERR_FAIL;
      }
   }
   return BLOCKTRACKING_BMAP_ERR_OK;
}

static BlockTrackingSparseBitmapError
BlockTrackingSparseBitmapUpdateStatistics(BlockTrackingSparseBitmap bitmap)
{
   uint16_t flag;
   BlockTrackingSparseBitmapVisitor updateStat =
      {UpdateStatVisitLeafNode, UpdateStatVisitInnerNode, NULL, NULL, NULL,
       &bitmap->_stat};
   assert(!TRIE_STAT_FLAG_IS_NULL(bitmap->_stat._flag));
   flag = bitmap->_stat._flag;
   memset(&bitmap->_stat, 0, sizeof(bitmap->_stat));
   bitmap->_stat._memoryInUse = sizeof(*bitmap);
   bitmap->_stat._flag = flag;
   return BlockTrackingSparseBitmapAccept(bitmap, 0, -1, &updateStat);
}


static BlockTrackingSparseBitmapError
BlockTrackingSparseBitmapDeserialize(BlockTrackingSparseBitmap bitmap,
                                     const char *stream, uint32_t streamLen)
{
   BlockTrackingSparseBitmapStream *begin =
      (BlockTrackingSparseBitmapStream *)stream;
   uint32_t len = streamLen / sizeof(*begin);
   BlockTrackingSparseBitmapStream *end = begin + len;
   BlockTrackingBitmapCallbackData data = {begin, end};
   BlockTrackingSparseBitmapVisitor deserialize =
      {DeserializeVisitLeafNode, DeserializeVisitInnerNode, NULL,
       DeserializeVisitNullNode, &data,
       (TRIE_STAT_FLAG_IS_NULL(bitmap->_stat._flag)) ? NULL : &bitmap->_stat
      };
   return BlockTrackingSparseBitmapAccept(bitmap, 0, -1, &deserialize);
}

static BlockTrackingSparseBitmapError
BlockTrackingSparseBitmapSerialize(BlockTrackingSparseBitmap bitmap,
                                   char *stream, uint32_t streamLen)
{
   BlockTrackingSparseBitmapError ret;
   BlockTrackingSparseBitmapStream *begin =
      (BlockTrackingSparseBitmapStream *)stream;
   uint32_t len = streamLen / sizeof(*begin);
   BlockTrackingSparseBitmapStream *end = begin + len;
   BlockTrackingBitmapCallbackData data = {begin, end};
   BlockTrackingSparseBitmapVisitor serialize =
      {SerializeVisitLeafNode, NULL, NULL, NULL, &data, NULL};
   ret = BlockTrackingSparseBitmapAccept(bitmap, 0, -1, &serialize);
   // append a terminator as the end of stream if the stream is not exhausted
   if (ret == BLOCKTRACKING_BMAP_ERR_OK) {
      begin = (BlockTrackingSparseBitmapStream *)data._cb;
      if (begin != end) {
         begin->_nodeOffset = -1;
      }
   }
   return ret;
}


static uint16_t
BlockTrackingSparseBitmapMakeTrieStatFlag(uint16_t mode)
{
   uint16_t flag = 0;
   if (mode & BLOCKTRACKING_BMAP_MODE_FAST_SET) {
      flag = 0;
   }
   if (mode & BLOCKTRACKING_BMAP_MODE_FAST_MERGE) {
      flag = 0;
   }
   if (mode & BLOCKTRACKING_BMAP_MODE_FAST_SERIALIZE) {
      flag |= TRIE_STAT_FLAG_LEAF;
   }
   if (mode & BLOCKTRACKING_BMAP_MODE_FAST_STATISTIC) {
      flag |= TRIE_STAT_FLAG_SET;
      flag |= TRIE_STAT_FLAG_MEM;
   }
   return flag;
}

////////////////////////////////////////////////////////////////////////////////
//   Public Interface
////////////////////////////////////////////////////////////////////////////////


BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_Create(BlockTrackingSparseBitmap *bitmap,
                                 uint16_t mode)
{
   if (bitmap == NULL) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }
   *bitmap = AllocateBitmap();
   if (*bitmap == NULL) {
      return BLOCKTRACKING_BMAP_ERR_OUT_OF_MEM;
   }
   (*bitmap)->_stat._flag = BlockTrackingSparseBitmapMakeTrieStatFlag(mode);
   if (TRIE_STAT_FLAG_IS_MEM((*bitmap)->_stat._flag)) {
      (*bitmap)->_stat._memoryInUse = sizeof(**bitmap);
   }
   return BLOCKTRACKING_BMAP_ERR_OK;
}

void
BlockTrackingSparseBitmap_Destroy(BlockTrackingSparseBitmap bitmap)
{
   if (bitmap != NULL) {
      BlockTrackingSparseBitmapDeleteTries(bitmap);
      FreeBitmap(bitmap);
   }
}

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_SetAt(BlockTrackingSparseBitmap bitmap, uint64_t addr,
                                Bool *oldValue)
{
   if (bitmap == NULL) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }

   return BlockTrackingSparseBitmapSetBit(bitmap, addr, oldValue);
}

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_SetInRange(BlockTrackingSparseBitmap bitmap,
                                     uint64_t fromAddr, uint64_t toAddr)
{
   if (bitmap == NULL || toAddr < fromAddr) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }

   return BlockTrackingSparseBitmapSetBits(bitmap, fromAddr, toAddr);
}

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_IsSet(BlockTrackingSparseBitmap bitmap,
                                uint64_t addr, Bool *isSet)
{
   if (bitmap == NULL || isSet == NULL) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }
   return BlockTrackingSparseBitmapQueryBit(bitmap, addr, isSet);
}

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_TraverseByBit(BlockTrackingSparseBitmap bitmap,
                                        uint64_t fromAddr, uint64_t toAddr,
                                        BlockTrackingSparseBitmapAccessBitCB cb,
                                        void *cbData)
{
   if (bitmap == NULL || cb == NULL || toAddr < fromAddr) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }
   return BlockTrackingSparseBitmapTraverseByBit(bitmap, fromAddr, toAddr,
                                                 cb, cbData);
}

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_TraverseByExtent(
                                   BlockTrackingSparseBitmap bitmap,
                                   uint64_t fromAddr, uint64_t toAddr,
                                   BlockTrackingSparseBitmapAccessExtentCB cb,
                                   void *cbData)
{
   if (bitmap == NULL || cb == NULL || toAddr < fromAddr) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }
   return BlockTrackingSparseBitmapTraverseByExtent(bitmap, fromAddr, toAddr,
                                                    cb, cbData);
}

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_Swap(BlockTrackingSparseBitmap bitmap1,
                               BlockTrackingSparseBitmap bitmap2)
{
   struct BlockTrackingSparseBitmap_t tmp;
   if (bitmap1 == NULL || bitmap2 == NULL) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }
   if (bitmap1 != bitmap2) {
      memcpy(&tmp, bitmap1, sizeof(tmp));
      memcpy(bitmap1, bitmap2, sizeof(tmp));
      memcpy(bitmap2, &tmp, sizeof(tmp));
   }
   return BLOCKTRACKING_BMAP_ERR_OK;
}

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_Merge(BlockTrackingSparseBitmap dest,
                                BlockTrackingSparseBitmap src)
{
   uint8_t i;
   TrieStatistics *stat;
   TrieVisitorReturnCode trieRetCode = TRIE_VISITOR_RET_CONT;

   if (dest == NULL || src == NULL) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
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


BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_Deserialize(BlockTrackingSparseBitmap bitmap,
                                      const char *stream, uint32_t streamLen)
{
   if (bitmap == NULL || stream == NULL || streamLen == 0) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }

   return BlockTrackingSparseBitmapDeserialize(bitmap, stream, streamLen);
}

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_Serialize(BlockTrackingSparseBitmap bitmap,
                                    char *stream, uint32_t streamLen)
{
   if (bitmap == NULL || stream == NULL || streamLen == 0) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }

   return BlockTrackingSparseBitmapSerialize(bitmap, stream, streamLen);
}

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_GetStreamMaxSize(uint64_t maxAddr,
                                           uint32_t *streamLen)
{
   if (streamLen == NULL) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }
   *streamLen = (maxAddr >> ADDR_BITS_IN_LEAF) + 1;
   if (*streamLen > MAX_NUM_LEAVES) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ADDR;
   }
   ++(*streamLen); // need a terminator
   *streamLen *= sizeof(BlockTrackingSparseBitmapStream);
   return BLOCKTRACKING_BMAP_ERR_OK;
}

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_GetStreamSize(BlockTrackingSparseBitmap bitmap,
                                        uint32_t *streamLen)
{
   BlockTrackingSparseBitmapError ret = BLOCKTRACKING_BMAP_ERR_OK;
   uint16_t leafCount;
   if (bitmap == NULL || streamLen == NULL) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }
   if (!TRIE_STAT_FLAG_IS_LEAF(bitmap->_stat._flag)) {
      TrieStatistics stat = bitmap->_stat;
      memset(&bitmap->_stat, 0, sizeof(bitmap->_stat));
      bitmap->_stat._flag |= TRIE_STAT_FLAG_LEAF;
      BlockTrackingSparseBitmapUpdateStatistics(bitmap);
      leafCount = bitmap->_stat._leafCount;
      bitmap->_stat = stat;
   } else {
      leafCount = bitmap->_stat._leafCount;
   }
   assert(leafCount <= MAX_NUM_LEAVES);
   ++leafCount; // need a terminator
   *streamLen = leafCount * sizeof(BlockTrackingSparseBitmapStream);
   return ret;
}

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_GetBitCount(BlockTrackingSparseBitmap bitmap,
                                      uint32_t *bitCount)
{
   if (bitmap == NULL || bitCount == 0) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }
   if (!TRIE_STAT_FLAG_IS_SET(bitmap->_stat._flag)) {
      TrieStatistics stat = bitmap->_stat;
      memset(&bitmap->_stat, 0, sizeof(bitmap->_stat));
      bitmap->_stat._flag |= TRIE_STAT_FLAG_SET;
      BlockTrackingSparseBitmapUpdateStatistics(bitmap);
      *bitCount = bitmap->_stat._totalSet;
      bitmap->_stat = stat;
   } else {
      *bitCount = bitmap->_stat._totalSet;
   }
   return BLOCKTRACKING_BMAP_ERR_OK;
}

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_GetMemoryInUse(BlockTrackingSparseBitmap bitmap,
                                         uint32_t *memoryInUse)
{
   if (bitmap == NULL || memoryInUse == 0) {
      return BLOCKTRACKING_BMAP_ERR_INVALID_ARG;
   }
   if (!TRIE_STAT_FLAG_IS_MEM(bitmap->_stat._flag)) {
      TrieStatistics stat = bitmap->_stat;
      memset(&bitmap->_stat, 0, sizeof(bitmap->_stat));
      bitmap->_stat._flag |= TRIE_STAT_FLAG_MEM;
      BlockTrackingSparseBitmapUpdateStatistics(bitmap);
      *memoryInUse = bitmap->_stat._memoryInUse;
      bitmap->_stat = stat;
   } else {
      *memoryInUse = bitmap->_stat._memoryInUse;
   }
   return BLOCKTRACKING_BMAP_ERR_OK;
}

