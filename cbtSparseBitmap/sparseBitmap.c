#include "cbtSparseBitmap.h"


#define MAX_NUM_TRIES 6
#define ADDR_BITS_IN_LEAF 9
#define ADDR_BITS_IN_INNER_NODE 3
#define ADDR_BITS_IN_HEIGHT(h) (ADDR_BITS_IN_LEAF+(h-1)*ADDR_BITS_IN_INNER_NODE)
#define NUM_TRIE_WAYS (1u << ADDR_BITS_IN_INNER_NODE)
#define TRIE_WAY_MASK (NUM_TRIE_WAYS-1)

#define COUNT_SET_BITS(x, c) \
   while((x) > 0) {          \
      ++(c);                 \
      (x) &= (x)-1;          \
   }

typedef union TrieNode_t {
   union TrieNode_t *_children[NUM_TRIE_WAYS];
   char _bitmap[NUM_TRIE_WAYS * sizeof(union TrieNode_t*)];
} *TrieNode;

#define LEAF_VALUE_MASK (sizeof(union TrieNode_t) * 8 - 1)

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


// visitor pattern
struct BlockTrackingSparseBitmapVisitor_t;
typedef void (*VisitTrieNode) (
      struct BlockTrackingSparseBitmapVisitor_t *visitor, uint64 addr,
      TrieNode node);
typedef void (*VisitNullNode) (
      struct BlockTrackingSparseBitmapVisitor_t *visitor, TrieNode *pNode);
typedef struct BlockTrackingSparseBitmapVisitor_t {
   VisitTrieNode _visitLeafNode;
   VisitTrieNode _beforeVisitInnerNode;
   VisitTrieNode _afterVisitInnerNode;
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

static Bool
TrieAccept(TrieNode *pNode, uint8 height, uint64 *fromAddr, uint64 toAddr,
           BlockTrackingSparseBitmapVisitor *visitor)
{
   assert(fromAddr <= toAddr);
   if (*pNode == NULL) {
      if (visitor->_visitNullNode == NULL) {
         // ignore NULL node and continue the traverse
         return TRUE;
      }
      if (!visitor->_visitNullNode(visitor, pNode)) {
         // TODO error
         return FALSE;
      }
   }
   if (height == 0) {
      // leaf
      uint64 maxAddr = (*fromAddr) | LEAF_VALUE_MASK;
      if (visitor->_visitLeafNode != NULL) {
         if (!visitor->_visitLeafNode(visitor, *fromAddr, *pNode)) {
            // TODO error
            return FALSE;
         }
      }
      // update fromAddr for next node in traverse
      if (toAddr < maxAddr) {
         maxAddr = toAddr;
      }
      *fromAddr = maxAddr+1;
   } else {
      // inner node
      uint64 nodeAddr = (*fromAddr) >> ADDR_BITS_IN_HEIGHT(height);
      uint8 way = nodeAddr & TRIE_WAY_MASK;
      nodeAddr <<= ADDR_BITS_IN_HEIGHT(height);
      if (visitor->_beforeVisitInnerNode != NULL) {
         if (!visitor->_beforeVisitInnerNode(visitor, nodeAddr, *pNode)) {
            // TODO error
            return FALSE;
         }
      }
      for (; way < NUM_TRIE_WAYS && (*fromAddr) <= toAddr ; ++way) {
         if (!TrieAccept((*pNode)->_children[way], height-1, fromAddr, toAddr,
                         visitor)) {
            // TODO error
            return FALSE;
         }
      }
      if (visitor->_afterVisitInnerNode != NULL) {
         if (!visitor->_afterVisitInnerNode(visitor, nodeAddr, *pNode)) {
            // TODO error
            return FALSE;
         }
      }
   }
   return TRUE;
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

static Bool
AllocateNodeVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                          TrieNode *pNode)
{
   *pNode = AllocateTrieNode(visitor->_stat);
   if (*pNode == NULL) {
      // TODO error
      return FALSE;
   }
   return TRUE;
}

/**
 * A visitor to query set bit
 */

static Bool
QueryBitVisitNullNode(BlockTrackingSparseBitmapVisitor *visitor,
                      TrieNode *pNode)
{
   Bool *isSet = (Bool *)visitor->_data;
   *isSet = FALSE;
   return FALSE;
}

static Bool
QueryBitVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64 addr, TrieNode node)
{
   Bool *isSet = (Bool *)visitor->_data;
   uint64 val = addr & LEAF_VALUE_MASK;
   uint8 byte = val / 8;
   uint8 bit = 1u << (val & 0x7);
   *isSet = node->_bitmap[byte] & bit;
   return TRUE;
}

/**
 * A visitor to set bit
 */

static Bool
SetBitVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                    uint64 addr, TrieNode node)
{
   Bool *isSet = (Bool *)visitor->_data;
   uint64 val = addr & LEAF_VALUE_MASK;
   uint8 byte = val / 8;
   uint8 bit = 1u << (val & 0x7);
   *isSet = node->_bitmap[byte] & bit;
   node->_bitmap[byte] |= bit;
   return TRUE;
}

/**
 * A visitor to set bits in range
 */

static Bool
SetBitsVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                     uint64 fromAddr, TrieNode node)
{
   uint64 toAddr = *(uint64 *)visitor->_data;
   uint64 val = fromAddr & LEAF_VALUE_MASK;
   uint8 byte = val / 8;
   uint8 bit = 1u << (val & 0x7);
   uint64 maxAddr = fromAddr | LEAF_VALUE_MASK;
   uint8 maxByte, maxBit;
   uint64 maxVal;
   if (toAddr < maxAddr) {
      maxAddr = toAddr;
   }
   if (maxAddr == fromAddr) {
      // fast pass for special case
      node->_bitmap[byte] |= bit;
      return TRUE;
   }
   maxVal = maxAddr & LEAF_VALUE_MASK;
   maxByte = maxVal / 8;
   maxBit = 1u << (maxVal & 0x7);
   // fill bits in the middle bytes
   if (maxByte - byte > 1) {
      memset(&node->_bitmap[byte+1], (int)-1, maxByte - byte - 1);
   }
   // fill the bits greater and equal to the bit
   node->_bitmap[byte] |= ~(bit-1);
   // fill the bits smaller and equal to the max bit
   node->_bitmap[maxByte] |= ~((~(maxBit-1)) << 1);
   return TRUE;
}

/**
 * A visitor to traverse bit
 */

static Bool
TraverseVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                      uint64 addr, TrieNode node)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData*)visitor->_data;
   BlockTrackingSparseBitmapAccessBitCB cb =
      (BlockTrackingSparseBitmapAccessBitCB)data->_cb;
   
   uint8 i;
   assert(sizeof(*node) % sizeof(uint64) == 0);
   uint64 *tmpBitmap = (uint64 *)node->_bitmap;
   for (i = 0 ; i < sizeof(*node) / sizeof(uint64) ;
        ++i, addr += sizeof(uint64) * 8) {
      uint64 c = tmpBitmap[i];
      uint8 bit = 0;
      while (c > 0) {
         if (c & 0x1 > 0) {
            if (!cb(data->_cbData, addr+bit)) {
               return FALSE;
            }
         }
         c >>= 1;
         ++bit;
      }
   }
   return TRUE;
}

/**
 *  A vistor to update statistics
 */

static Bool 
UpdateStatVisitInnerNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 addr, TrieNode node)
{
   visitor->_stat->_memoryInUse += sizeof(*node);
   return TRUE;
}

static Bool
UpdateStatVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                        uint64 addr, TrieNode node)
{
   BitmapStatistics *stat = visitor->_stat;
   uint8 i;
   assert(stat != NULL);
   stat->_memoryInUse += sizeof(*node);
   assert(sizeof(*node) % sizeof(uint64) == 0);
   uint64 *tmpBitmap = (uint64 *)node->_bitmap;
   for (i = 0 ; i < sizeof(*node) / sizeof(uint64) ; ++i) {
      COUNT_SET_BITS(tmpBitmap[i], stat->_totalSet);
   }
   return TRUE;
}

/**
 *  A vistor to delete trie
 */

static Bool
DeleteTrieNode(BlockTrackingSparseBitmapVisitor *visitor,
               uint64 addr, TrieNode node)
{
   FreeTrieNode(node, (BitmapStatistics *)visitor->_data);
   return TRUE;
}


/**
 *  A visitor to deserialize
 */

static Bool
DeserializeVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                         uint64 addr, TrieNode node)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData *)visitor->_data;
   TrieNode srcNode = (TrieNode)data->_cb;
   uint32 offset = *(uint32 *)data->_cbData;
   TrieLeafNodeMergeFlatBitmap(node, (const char *)srcNode[offset]->_bitmap,
                               visitor->_stat);
   // update the src offset
   ++offset;
   *(uint32 *)data->_cbData = offset;
   return TRUE;
}

/**
 *  A vistor to serialize
 */

static Bool
SerializeVisitLeafNode(BlockTrackingSparseBitmapVisitor *visitor,
                       uint64 addr, TrieNode node)
{
   BlockTrackingBitmapCallbackData *data =
      (BlockTrackingBitmapCallbackData*)visitor->_data;
   char *buf = (char*)data->_cb;
   uint32 bufLen = *(uint32*)data->_cbData;
   addr /= 8;
   if (addr + sizeof(*node) <= bufLen) {
      memcpy(&buf[addr], node->_bitmap, sizeof(*node));
      return TRUE;
   } else {
      return FALSE;
   }
}

////////////////////////////////////////////////////////////////////////////////
//   BlockTrackingSparseBitmap Internal Functions
////////////////////////////////////////////////////////////////////////////////

/**
 * accept visitor
 */

static void
BlockTrackingSparseBitmapAccept(const BlockTrackingSparseBitmap bitmap,
                                uint64 fromAddr, uint64 toAddr
                                BlockTrackingSparseBitmapVisitor *visitor)
{
   uint8 i = TrieMaxHeight(fromAddr);
   Bool pass = TRUE;
   if (!TrieIndexValidation(i)) {
      // TODO error
   }

   for (; i < MAX_NUM_TRIES && pass && (*fromAddr) <= toAddr ; ++i) {
      pass = TrieAccept(&bitmap->_tries[i], i, &fromAddr, toAddr, visitor);
   }
   if (!pass) {
      // TODO error
   }
}

void
BlockTrackingSparseBitmapDeleteTries(BlockTrackingSparseBitmap bitmap)
{
   BlockTrackingSparseBitmapVisitor deleteTrie =
      {DeleteTrieNode, NULL, DeleteTrieNode, NULL, NULL,
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
      {SetBitsVisitLeafNode, NULL, NULL, AllocateNodeVisitNullNode, &toAddr,
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
                                     uint64 fromAddr, uint64 toAddr,
                                     TrieNode node)
{
   uint32 offset = 0;
   BlockTrackingBitmapCallbackData data = {node, &offset};
   BlockTrackingSparseBitmapVisitor deserialize =
      {DeserializeVisitLeafNode, NULL, NULL,
       AllocateNodeVisitNullNode, &data,
#ifdef CBT_SPARSE_BITMAP_DEBUG
       &bitmap->_stat
#else
       NULL
#endif
      };
   BlockTrackingSparseBitmapAccept(dest, fromAddr, toAddr, &deserialize);
}

void
BlockTrackingSparseBitmapSerialize(const BlockTrackingSparseBitmap bitmap,
                                   char *flatBitmap, uint32 flatBitmapLen)
{
   BlockTrackingBitmapCallbackData data = {flatBitmap, &flatBitmapLen};
   BlockTrackingSparseBitmapVisitor serialize =
      {SerializeVisitLeafNode, NULL, NULL, NULL, &data,
#ifdef CBT_SPARSE_BITMAP_DEBUG
       &bitmap->_stat
#else
       NULL
#endif
      };
   BlockTrackingSparseBitmapAccept(dest, 0, -1, &serialize);
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
                                      const char *flatBitmap,
                                      uint32 flatBitmapLen)
{
   uint32 i;
   uint32 len;
   TrieNode node;
   union TrieNode_t zeroNode = {0};
   if (bitmap == NULL || flatBitmap == NULL) {
      // TODO error
   }
   node = (TrieNode)flatBitmap;
   len = flatBitmapLen / sizeof(*node);
   // TODO check len

   for (i = 0 ; i < len ; ++i) {
   {
      uint64 fromAddr, toAddr;
      uint64 j;
      // skip zero nodes
      if (memcmp(&node[i], &zeroNode, sizeof(zeroNode)) == 0) {
         continue;
      }
      fromAddr = i;
      fromAddr <<= ADDR_BITS_IN_LEAF;
      // get all continuous non-zero nodes
      for (j = i+1 ; j < len ; ++j) {
         if (memcmp(&node[j], &zeroNode, sizeof(zeroNode)) == 0) {
            break;
         }
      }
      toAddr = j-1;
      toAddr <<= ADDR_BITS_IN_LEAF;
      BlockTrackingSparseBitmapDeserialize(bitmap, fromAddr, toAddr, &node[i]);
      i = j;
   }
   // last piece
   len = flatBitmapLen - i * sizeof(*node);
   if (len > 0) {
      if (memcmp(&node[i], &zeroNode, len) != 0) {
         uint64 addr = i;
         addr <<= ADDR_BITS_IN_LEAF;
         memcpy(&zeroNode, &node[i], len);
         BlockTrackingSparseBitmapDeserialize(bitmap, addr, addr, &zeroNode);
      }
   }
}
                                 
void
BlockTrackingSparseBitmap_Serialize(const BlockTrackingSparseBitmap bitmap,
                                    char *flatBitmap, uint32 flatBitmapLen)
{
   if (bitmap == NULL || buf == NULL) {
      // TODO error
   }

   BlockTrackingSparseBitmapSerialize(bitmap, flatBitmap, flatBitmapLen);
}

