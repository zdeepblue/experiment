#include "cbtSparseBitmap.h"


#define MAX_NUM_TRIES 6
#define ADDR_BITS_IN_LEAF 9
#define ADDR_BITS_IN_INNER_NODE 3
#define ADDR_BITS_IN_HEIGHT(h) (ADDR_BITS_IN_LEAF+(h-1)*ADDR_BITS_IN_INNER_NODE)
#define NUM_TRIE_WAYS (1u << ADDR_BITS_IN_INNER_NODE)
#define TRIE_WAY_MASK ((uint64)NUM_TRIE_WAYS-1)

#define COUNT_SET_BITS(x, c) \
   while((x) > 0) {          \
      ++(c);                 \
      (x) &= (x)-1;          \
   }

#define GET_BITMAP_BYTE_BIT(offset, byte, bit) \
   do {                                        \
      byte = offset >> 3;                      \
      bit = 1u << (offset & 0x7)               \
   } while (0)

typedef union TrieNode_t {
   union TrieNode_t *_children[NUM_TRIE_WAYS];
   char _bitmap[NUM_TRIE_WAYS * sizeof(union TrieNode_t*)];
} *TrieNode;

#define LEAF_VALUE_MASK ((uint64)sizeof(union TrieNode_t) * 8 - 1)
#define NODE_ADDR_MASK(h) (~(((uint64)1ull << ADDR_BITS_IN_HEIGHT(h)) - 1))
#define GET_NODE_WAYS(addr, h) ((addr>>ADDR_BITS_IN_HEIGHT(h)) & TRIE_WAY_MASK)

typedef struct BitmapStatistics_t {
   uint32 _totalSet;
   uint32 _memoryUsage;
} BitmapStatistics;

struct BlockTrackingSparseBitmap_t {
   TrieNode _tries[MAX_NUM_TRIES];
   BitmapStatistics _stat;
   uint64 _padding;
};

typedef struct BlockTrackingBitmapCallbackData_t {
   void *_cb;
   void *_cbData;
} BlockTrackingBitmapCallbackData;


// pack it to avoid waste bytes for allignment
typedef struct BlockTrackingBitmapStream_t {
   uint16 _nodeOffset;
   union TrieNode_t _node;
} __attribute__((packed)) BlockTrackingBitmapStream;

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
      uint64 nodeAddr, uint8 height, TrieNode node);

typedef TrieVisitorReturnCode (*VisitLeafNode) (
      struct BlockTrackingSparseBitmapVisitor_t *visitor,
      uint64 nodeAddr, uint16 fromOffset, uint16 toOffset, TrieNode node);

typedef TrieVisitorReturnCode (*VisitNullNode) (
      struct BlockTrackingSparseBitmapVisitor_t *visitor, uint64 nodeAddr,
      uint8 height, TrieNode *pNode);

typedef struct BlockTrackingSparseBitmapVisitor_t {
   VisitLeafNode _visitLeafNode;
   VisitInnerNode _beforeVisitInnerNode;
   VisitInnerNode _afterVisitInnerNode;
   VisitNullNode _visitNullNode;
   void *_data;
   BitmapStatistics *_stat;
} BlockTrackingSparseBitmapVisitor;

////////////////////////////////////////////////////////////////////////////////
//   Allocate/Free Functions
////////////////////////////////////////////////////////////////////////////////

static TrieNode
AllocateTrieNode(BitmapStatistics *stat)
{
   TrieNode node = (TrieNode)calloc(1, sizeof(*node));
   if (stat != NULL && node != NULL) {
      stat->_memoryInUse += sizeof(*node);
   }
   return node;
}

static void
FreeTrieNode(TrieNode node, BitmapStatistics *stat)
{
   free(node);
   if (stat != NULL) {
      stat->_memoryInUse -= sizeof(*node);
   }
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

static void
TrieLeafNodeMergeFlatBitmap(TrieNode destNode, const char *flatBitmap,
                            BitmapStatistics *stat)
{
   uint8 i;
   uint64 *destBitmap = (uint64*)destNode->_bitmap;
   const uint64 *srcBitmap = (const uint64*)flatBitmap;
   static_assert(sizeof(*node) % sizeof(uint64) == 0);
   if (stat != NULL) {
      for (i = 0 ; i < sizeof(*node)/sizeof(uint64) ; ++i) {
         uint64 o = destBitmap[i];
         uint64 n = o | srcBitmap[i];
         o ^= n;
         COUNT_SET_BITS(o, stat->_totalSet);
         destBitmap[i] = n;
      }
   } else {
      for (i = 0 ; i < sizeof(*node)/sizeof(uint64) ; ++i) {
         destBitmap[i] |= srcBitmap[i];
      }
   }
}

static void
TrieMerge(TrieNode *pDestNode, TrieNode *pSrcNode, uint8 height)
{
   if (*pSrcNode == NULL) {
      // nothing to merge from src
      return;
   }
   if (*pDestNode == NULL) {
      // move the trie node from src to dest
      *pDestNode = *pSrcNode;
      *pSrcNode = NULL;
   } else {
      if (height == 0) {
         // leaf
         TrieLeafNodeMergeFlatBitmap(*pDestNode, (*pSrcNode)->_bitmap, NULL);
      } else {
         // inner node
         uint8 way;
         for (way = 0 ; way < NUM_TRIE_WAYS ; ++way) {
            TrieMerge(&(*pDestNode)->_children[way],
                      &(*pSrcNode)->_children[way],
                      height-1);
         }
      }
   }
}

static TrieVisitorReturnCode
TrieAccept(TrieNode *pNode, uint8 height, uint64 *fromAddr, uint64 toAddr,
           BlockTrackingSparseBitmapVisitor *visitor)
{
   TrieVisitorReturnCode ret = TRIE_VISITOR_RET_CONT;
   assert(fromAddr <= toAddr);
   if (*pNode == NULL) {
      uint64 nodeAddr;
      if (visitor->_visitNullNode == NULL) {
         // ignore NULL node and continue the traverse
         return ret;
      }
      nodeAddr = (height == 0) ?
                     (*fromAddr) & ~LEAF_VALUE_MASK :
                     (*fromAddr) & NODE_ADDR_MASK(height);
      if ((ret = visitor->_visitNullNode(visitor, nodeAddr, height, pNode))
            != TRIE_VISITOR_RET_CONT) {
         return ret;
      }
   }
   if (height == 0) {
      // leaf
      uint64 nodeAddr = (*fromAddr) & ~LEAF_VALUE_MASK :
      uint64 maxAddr = (*fromAddr) | LEAF_VALUE_MASK;
      uint16 fromOffset =
         (*fromAddr > nodeAddr) ? (*fromAddr) & LEAF_VALUE_MASK : 0;
      uint16 toOffset =
         (toAddr > maxAddr) ? LEAF_VALUE_MASK : toAddr & LEAF_VALUE_MASK;
      if (visitor->_visitLeafNode != NULL) {
         if ((ret = visitor->_visitLeafNode(visitor, nodeAddr, height,
                                            fromOffset, toOffset, *pNode))
               != TRIE_VISITOR_RET_CONT) {
            return ret;
         }
      }
      // update fromAddr for next node in traverse
      if (toAddr < maxAddr) {
         maxAddr = toAddr;
      }
      *fromAddr = maxAddr+1;
   } else {
      // inner node
      uint64 nodeAddr = (*fromAddr) & NODE_ADDR_MASK(height);
      uint8 way;
      if (visitor->_beforeVisitInnerNode != NULL) {
         if ((ret = visitor->_beforeVisitInnerNode(visitor, nodeAddr, height,
                                                   *pNode))
               != TRIE_VISITOR_RET_CONT) {
            return ret;
         }
      }
      for (way = GET_NODE_WAYS(nodeAddr, height);
           way < NUM_TRIE_WAYS && (*fromAddr) <= toAddr;
           ++way) {
         ret = TrieAccept((*pNode)->_children[way], height-1, fromAddr, toAddr,
                          visitor);
         if (ret != TRIE_VISITOR_RET_CONT &&
             ret != TRIE_VISITOR_RET_SKIP_CHILDREN) {
            return ret;
         }
      }
      if (visitor->_afterVisitInnerNode != NULL) {
         if ((ret = visitor->_afterVisitInnerNode(visitor, nodeAddr, height,
                                                  *pNode))
               != TRIE_VISITOR_RET_CONT) {
            return ret;
         }
      }
   }
   return ret;
}

static uint8
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

static TrieVisitorReturnCode
AllocateNodeVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                          uint64 nodeAddr, uint8 height,
                          TrieNode *pNode)
{
   *pNode = AllocateTrieNode(visitor->_stat);
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
                      uint64 nodeAddr, uint8 height,
                      TrieNode *pNode)
{
   Bool *isSet = (Bool *)visitor->_data;
   *isSet = FALSE;
   return TRIE_VISITOR_RET_END;
}

static TrieVisitorReturnCode
QueryBitVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                      TrieNode node)
{
   Bool *isSet = (Bool *)visitor->_data;
   uint8 byte, bit;
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   *isSet = node->_bitmap[byte] & bit;
   return TRIE_VISITOR_RET_END;
}

/**
 * A visitor to set bit
 */

static TrieVisitorReturnCode
SetBitVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                    uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                    TrieNode node)
{
   Bool *isSet = (Bool *)visitor->_data;
   uint8 byte, bit;
   assert(fromOffset == toOffset);
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   *isSet = node->_bitmap[byte] & bit;
   node->_bitmap[byte] |= bit;
   return TRIE_VISITOR_RET_END;
}

/**
 * A visitor to set bits in range
 */

static TrieVisitorReturnCode
SetBitsVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                     uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                     TrieNode node)
{
   uint8 byte, bit;
   uint8 toByte, toBit;
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   if (toOffset == fromOffset) {
      // fast pass for special case
      node->_bitmap[byte] |= bit;
      return TRIE_VISITOR_RET_CONT;
   }
   GET_BITMAP_BYTE_BIT(toOffset, toByte, toBit);
   // fill bits in the middle bytes
   if (toByte - byte > 1) {
      memset(&node->_bitmap[byte+1], (int)-1, toByte - byte - 1);
   }
   // fill the bits greater and equal to the bit
   node->_bitmap[byte] |= ~(bit-1);
   // fill the bits smaller and equal to the max bit
   node->_bitmap[toByte] |= ~((~(toBit-1)) << 1);
   return TRIE_VISITOR_RET_CONT;
}

/**
 * A visitor to traverse bit
 */

static TrieVisitorReturnCode
TraverseVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                      TrieNode node)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData*)visitor->_data;
   BlockTrackingSparseBitmapAccessBitCB cb =
      (BlockTrackingSparseBitmapAccessBitCB)data->_cb;
   
   uint8 i;
   uint8 byte, bit;
   uint8 toByte, toBit;
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   GET_BITMAP_BYTE_BIT(toOffset, toByte, toBit);

   char *bitmap = node->_bitmap;
   for (i = byte, nodeAddr += i * 8 ; i <= toByte ; ++i, nodeAddr += 8) {
      char c = bitmap[i];
      uint b = (i == byte) ? bit : 0;
      uint e = (i == toByte) ? toBit : 7;
      while (c > 0 && b <= e) {
         if (c & 0x1 > 0) {
            if (!cb(data->_cbData, nodeAddr+b)) {
               return TRIE_VISITOR_RET_ABORT;
            }
         }
         c >>= 1;
      }
   }

   return TRIE_VISITOR_RET_CONT;
}

/**
 *  A vistor to update statistics
 */

static TrieVisitorReturnCode
UpdateStatVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 nodeAddr, uint8 height, TrieNode node)
{
   visitor->_stat->_memoryInUse += sizeof(*node);
   return TRIE_VISITOR_RET_CONT;
}

static TrieVisitorReturnCode
UpdateStatVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                        uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                        TrieNode node)
{
   BitmapStatistics *stat = visitor->_stat;
   uint8 i;
   char *bitmap;
   uint8 byte, bit;
   uint8 toByte, toBit;
   GET_BITMAP_BYTE_BIT(fromOffset, byte, bit);
   GET_BITMAP_BYTE_BIT(toOffset, toByte, toBit);
   assert(stat != NULL);
   stat->_memoryInUse += sizeof(*node);
   bitmap = node->_bitmap;
   for (i = byte ; i <= toByte  ; ++i) {
      char c = bitmap[i];
      if (i == byte) {
         c &= ~(bit-1);
      } else if (i == toByte) {
         c &= ~((~(bit-1)) << 1);
      }
      COUNT_SET_BITS(c, stat->_totalSet);
   }
   return TRIE_VISITOR_RET_CONT;
}

/**
 *  A vistor to delete trie
 */

static TrieVisitorReturnCode
DeleteInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
               uint64 nodeAddr, uint8 height, TrieNode node)
{
   FreeTrieNode(node, visitor->_stat);
   return TRIE_VISITOR_RET_CONT;
}

static TrieVisitorReturnCode
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

static TrieVisitorReturnCode
DeserializeNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                    uint64 nodeAddr, uint8 height, TrieNode *pNode)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData *)visitor->_data;
   const BlockTrackingSparseBitmapStream* stream =
      (const BlockTrackingSparseBitmapStream*)data->_cb;

   uint64 targetNodeAddr = stream->_nodeOffset << ADDR_BITS_IN_LEAF;
   uint64 maxNodeAddr = (height == 0) ?
                              nodeAddr | LEAF_VALUE_MASK :
                              nodeAddr | ~NODE_ADDR_MASK(height);

   assert(targetNodeAddr >= nodeAddr);
   if (targetNodeAddr > maxNodeAddr) {
      return TRIE_VISITOR_RET_SKIP_CHILDREN;
   } else {
      if (*pNode == NULL) {
         if ((*pNode = AllocateTrieNode(visitor->_stat)) == NULL) {
            return TRIE_VISITOR_RET_OUT_OF_MEM;
         }
      }
      return TRIE_VISITOR_RET_CONT;
   }
}

static TrieVisitorReturnCode
DeserializeInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                     uint64 nodeAddr, uint8 height, TrieNode node)
{
   DeserializeNullNode(visitor, nodeAddr, height, &node);
}

static TrieVisitorReturnCode
DeserializeVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                         TrieNode node)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData *)visitor->_data;
   const BlockTrackingSparseBitmapStream* stream =
      (const BlockTrackingSparseBitmapStream*)data->_cb;
   const BlockTrackingSparseBitmapStream* streamEnd =
      (const BlockTrackingSparseBitmapStream*)data->_cbData;

   if (stream != streamEnd && stream->_nodeOffset != -1) {
      TrieLeafNodeMergeFlatBitmap(node, (const char *)stream->_node->_bitmap,
                                  visitor->_stat);
      // to the next stream
      ++stream;
      data->_cb = stream;
      return TRIE_VISITOR_RET_CONT;
   } else {
      return TRIE_VISITOR_RET_END;
   }
}

/**
 *  A vistor to serialize
 */

static TrieVisitorReturnCode
SerializeVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                       uint64 nodeAddr, uint16 fromOffset, uint16 toOffset,
                       TrieNode node)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData*)visitor->_data;
   BlockTrackingSparseBitmapStream* stream =
      (BlockTrackingSparseBitmapStream*)data->_cb;
   BlockTrackingSparseBitmapStream* streamEnd =
      (BlockTrackingSparseBitmapStream*)data->_cbData;
   uint16 nodeOffset = nodeAddr >> ADDR_BITS_IN_LEAF;

   if (stream != streamEnd) {
      stream->_nodeOffset = nodeOffset;
      memcpy(stream->_node._bitmap, node->_bitmap, sizeof(*node));
      // to the next stream
      ++stream;
      data->_cb = stream;
      return TRIE_VISITOR_RET_CONT;
   } else {
      return TRIE_VISITOR_RET_OVERFLOW;
   }
}

////////////////////////////////////////////////////////////////////////////////
//   BlockTrackingSparseBitmap Internal Functions
////////////////////////////////////////////////////////////////////////////////

/**
 * accept a visitor
 */

static void
BlockTrackingSparseBitmapAccept(const BlockTrackingSparseBitmap bitmap,
                                uint64 fromAddr, uint64 toAddr
                                BlockTrackingSparseBitmapVisitor *visitor)
{
   uint8 i = TrieMaxHeight(fromAddr);
   TrieVisitorReturnCode ret; 
   if (!TrieIndexValidation(i)) {
      // TODO error
   }

   for (; i < MAX_NUM_TRIES && (*fromAddr) <= toAddr ; ++i) {
      ret = TrieAccept(&bitmap->_tries[i], i, &fromAddr, toAddr, visitor);
      if (ret != TRIE_VISITOR_RET_CONT &&
          ret != TRIE_VISITOR_RET_SKIP_CHILDREN) {
         break;
      }
   }
   // TODO error handling
}

void
BlockTrackingSparseBitmapDeleteTries(BlockTrackingSparseBitmap bitmap)
{
   BlockTrackingSparseBitmapVisitor deleteTrie =
      {DeleteLeafNode, NULL, DeleteInnerNode, NULL, NULL,
#ifdef CBT_SPARSE_BITMAP_DEBUG
       &bitmap->_stat
#else
       NULL
#endif
      };
   BlockTrackingSparseBitmapAccept(bitmap, 0, -1, &deleteTrie);
#ifdef CBT_SPARSE_BITMAP_DEBUG
   assert(bitmap->_stat._memoryInUse == sizeof(*bitmap));
#endif
}

Bool
BlockTrackingSparseBitmapSetBit(BlockTrackingSparseBitmap bitmap, uint64 addr)
{
   Bool isSetBefore = FALSE;
   BlockTrackingSparseBitmapVisitor setBit =
      {SetBitVisitLeafNode, NULL, NULL,
       AllocateNodeVisitNullNode, &isSetBefore,
#ifdef CBT_SPARSE_BITMAP_DEBUG
       &bitmap->_stat
#else
       NULL
#endif
      };

   BlockTrackingSparseBitmapAccept(bitmap, addr, addr, &setBit);

#ifdef CBT_SPARSE_BITMAP_DEBUG
   if (!isSetBefore) {
      bitmap->_stat._totalSet++;
   }
#endif
   return isSetBefore;
}

void
BlockTrackingSparseBitmapSetBits(BlockTrackingSparseBitmap bitmap,
                                 uint64 fromAddr, uint64 toAddr)
{
   BlockTrackingSparseBitmapVisitor setBits =
      {SetBitsVisitLeafNode, NULL, NULL, AllocateNodeVisitNullNode, NULL,
#ifdef CBT_SPARSE_BITMAP_DEBUG
       &bitmap->_stat
#else
       NULL
#endif
      };

   BlockTrackingSparseBitmapAccept(bitmap, fromAddr, toAddr, &setBits);
}

Bool
BlockTrackingSparseBitmapQueryBit(const BlockTrackingSparseBitmap bitmap,
                                  uint64 addr)
{
   Bool isSetBefore = FALSE;
   BlockTrackingSparseBitmapVisitor queryBit =
      {QueryBitVisitLeafNode, NULL, NULL,
       QueryBitVisitNullNode, &isSetBefore,
#ifdef CBT_SPARSE_BITMAP_DEBUG
       &bitmap->_stat
#else
       NULL
#endif
      };
   BlockTrackingSparseBitmapAccept(bitmap, addr, addr, &queryBit);
   return isSetBefore;
}


void
BlockTrackingSparseBitmapTraverse(const BlockTrackingSparseBitmap bitmap,
                                  uint64 fromAddr, uint64 toAddr,
                                  BlockTrackingSparseBitmapAccessBitCB cb,
                                  void *cbData)
{
   BlockTrackingBitmapCallbackData data = {cb, cbData};
   BlockTrackingSparseBitmapVisitor traverse =
      {TraverseVisitLeafNode, NULL, NULL, NULL, NULL,
#ifdef CBT_SPARSE_BITMAP_DEBUG
       &bitmap->_stat
#else
       NULL
#endif
      };
   BlockTrackingSparseBitmapAccept(bitmap, fromAddr, toAddr, &traverse);
}

void
BlockTrackingSparseBitmapUpdateStatistics(BlockTrackingSparseBitmap bitmap)
{
   BlockTrackingSparseBitmapVisitor updateStat =
      {UpdateStatVisitLeafNode, UpdateStatVisitInnerNode, NULL, NULL, NULL,
       &bitmap->_stat};
   memset(&bitmap->_stat, 0, sizeof(bitmap->_stat));
   bitmap->_stat._memoryInUse = sizeof(*bitmap);
   BlockTrackingSparseBitmapAccept(dest, 0, -1, &updateStat);
}


void
BlockTrackingSparseBitmapDeserialize(BlockTrackingSparseBitmap bitmap,
                                     const char *stream, uint32 streamLen)
{
   BlockTrackingBitmapStream *begin = (BlockTrackingBitmapStream *)stream;
   uint32 len = streamLen / sizeof(*begin);
   BlockTrackingBitmapStream *end = stream + len;
   BlockTrackingBitmapCallbackData data = {begin, end};
   BlockTrackingSparseBitmapVisitor deserialize =
      {DeserializeVisitLeafNode, DeserializeVisitInnerNode, NULL,
       DeserializeVisitNullNode, &data,
#ifdef CBT_SPARSE_BITMAP_DEBUG
       &bitmap->_stat
#else
       NULL
#endif
      };
   BlockTrackingSparseBitmapAccept(dest, 0, -1, &deserialize);
}

void
BlockTrackingSparseBitmapSerialize(const BlockTrackingSparseBitmap bitmap,
                                   char *stream, uint32 streamLen)
{
   BlockTrackingBitmapStream *begin = (BlockTrackingBitmapStream *)stream;
   uint32 len = streamLen / sizeof(*begin);
   BlockTrackingBitmapStream *end = stream + len;
   BlockTrackingBitmapCallbackData data = {begin, end};
   BlockTrackingSparseBitmapVisitor serialize =
      {SerializeVisitLeafNode, NULL, NULL, NULL, &data,
#ifdef CBT_SPARSE_BITMAP_DEBUG
       &bitmap->_stat
#else
       NULL
#endif
      };
   BlockTrackingSparseBitmapAccept(dest, 0, -1, &serialize);
   // append a terminal as the end of stream if the stream is not exhausted
   begin = (BlockTrackingBitmapStream *)data._cb;
   if (begin != end) {
      begin->_nodeOffset = -1;
   }
}


////////////////////////////////////////////////////////////////////////////////
//   Public Interface
////////////////////////////////////////////////////////////////////////////////

BlockTrackingSparseBitmap
BlockTrackingSparseBitmap_Create()
{
   BlockTrackingSparseBitmap bitmap = AllocateBitmap();
   if (bitmap == NULL) {
      // TODO error
   }
#ifdef CBT_SPARSE_BITMAP_DEBUG
   bitmap->_stat._memoryInUse = sizeof(*bitmap);
#endif
   return bitmap;
}

void
BlockTrackingSparseBitmap_Destroy(BlockTrackingSparseBitmap bitmap)
{
   if (bitmap == NULL) {
      // TODO error
   }
   BlockTrackingSparseBitmapDeleteTries(bitmap);
   FreeBitmap(bitmap);
}

Bool
BlockTrackingSparseBitmap_SetAt(BlockTrackingSparseBitmap bitmap, uint64 addr)
{
   if (bitmap == NULL) {
      // TODO error
      return FALSE;
   }

   return BlockTrackingSparseBitmapSetBit(bitmap, addr);
}

void
BlockTrackingSparseBitmap_SetInRange(BlockTrackingSparseBitmap bitmap,
                                     uint64 fromAddr, uint64 toAddr)
{
   if (bitmap == NULL || toAdd < fromAddr) {
      // TODO error
   }

   BlockTrackingSparseBitmapSetBits(bitmap, fromAddr, toAddr);
}

Bool
BlockTrackingSparseBitmap_IsSet(const BlockTrackingSparseBitmap bitmap,
                                uint64 addr)
{
   if (bitmap == NULL) {
      // TODO error
      return FALSE;
   }
   return BlockTrackingSparseBitmapQueryBit(bitmap, addr);
}

void
BlockTrackingSparseBitmap_Traverse(const BlockTrackingSparseBitmap bitmap,
                                   uint64 fromAddr, uint64 toAddr,
                                   BlockTrackingSparseBitmapAccessBitCB cb,
                                   void *cbData)
{
   if (bitmap == NULL || cb == NULL || toAddr < fromAddr) {
      // TODO error
   }
   BlockTrackingSparseBitmapTraverse(bitmap, fromAddr, toAddr, cb, cbData);
}

void
BlockTrackingSparseBitmap_Swap(BlockTrackingSparseBitmap bitmap1,
                               BlockTrackingSparseBitmap bitmap2)
{
   struct BlockTrackingSparseBitmap_t tmp;
   if (bitmap1 == NULL || bitmap2 == NULL) {
      // TODO error handling
   }
   if (bitmap1 == bitmap2) {
      return;
   }
   memcpy(&tmp, bitmap1, sizeof(tmp));
   memcpy(bitmap1, bitmap2, sizeof(tmp));
   memcpy(bitamp2, &tmp, sizeof(tmp));
}

void
BlockTrackingSparseBitmap_Merge(BlockTrackingSparseBitmap dest,
                                BlockTrackingSparseBitmap src)
{
   uint8 i;
   if (dest == NULL || src == NULL) {
      // TODO error handling
   }
   for (i = 0; i < MAX_NUM_TRIES ; ++i) {
      TrieMerge(&dest->_tries[i], &src->_tries[i], i);
   }
   BlockTrackingSparseBitmap_Destroy(src);
#ifdef CBT_SPARSE_BITMAP_DEBUG
   BlockTrackingSparseBitmapUpdateStatistics(dest);
#endif
}


void
BlockTrackingSparseBitmap_Deserialize(BlockTrackingSparseBitmap bitmap,
                                      const char *stream, uint32 streamLen)
{
   uint32 i;
   uint32 len;
   TrieNode node;
   if (bitmap == NULL || stream == NULL) {
      // TODO error
   }

   BlockTrackingSparseBitmapDeserialize(bitmap, stream, streamLen);
}
                                 
void
BlockTrackingSparseBitmap_Serialize(const BlockTrackingSparseBitmap bitmap,
                                    char *stream, uint32 streamLen)
{
   if (bitmap == NULL || stream == NULL || streamLen == 0) {
      // TODO error
   }

   BlockTrackingSparseBitmapSerialize(bitmap, stream, streamLen);
}

