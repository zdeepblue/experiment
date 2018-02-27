#ifndef _CBT_SPARSE_BITMAP_H_
#define _CBT_SPARSE_BITMAP_H_

#include <stdint.h>
#define Bool int
#define TRUE 1
#define FALSE 0

typedef Bool (*BlockTrackingSparseBitmapAccessBitCB)(void *cbData,
                                                     uint64_t addr);
typedef Bool (*BlockTrackingSparseBitmapAccessExtentCB)(void *cbData,
                                                        uint64_t start,
                                                        uint64_t end);

struct BlockTrackingSparseBitmap_t;
typedef struct BlockTrackingSparseBitmap_t *BlockTrackingSparseBitmap;

typedef enum {
   BLOCKTRACKING_BMAP_ERR_OK = 0,
   BLOCKTRACKING_BMAP_ERR_INVALID_ARG,
   BLOCKTRACKING_BMAP_ERR_INVALID_ADDR,
   BLOCKTRACKING_BMAP_ERR_OUT_OF_RANGE,
   BLOCKTRACKING_BMAP_ERR_OUT_OF_MEM,
   BLOCKTRACKING_BMAP_ERR_FAIL
} BlockTrackingSparseBitmapError;


#define BLOCKTRACKING_BMAP_MODE_FAST_SET 1
#define BLOCKTRACKING_BMAP_MODE_FAST_MERGE 2
#define BLOCKTRACKING_BMAP_MODE_FAST_SERIALIZE 4
#define BLOCKTRACKING_BMAP_MODE_FAST_STATISTIC 8

// constructor and destructor
BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_Create(BlockTrackingSparseBitmap *bitmap,
                                 uint16_t mode);

void
BlockTrackingSparseBitmap_Destroy(BlockTrackingSparseBitmap bitmap);

// set and query
BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_SetAt(BlockTrackingSparseBitmap bitmap, uint64_t addr,
                                Bool *oldValue);

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_SetInRange(BlockTrackingSparseBitmap bitmap,
                                     uint64_t fromAddr, uint64_t toAddr);

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_IsSet(BlockTrackingSparseBitmap bitmap,
                                uint64_t addr,
                                Bool *oldValue);

// traverse
BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_TraverseByBit(BlockTrackingSparseBitmap bitmap,
                                        uint64_t fromAddr, uint64_t toAddr,
                                        BlockTrackingSparseBitmapAccessBitCB cb,
                                        void *cbData);

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_TraverseByExtent(
                                   BlockTrackingSparseBitmap bitmap,
                                   uint64_t fromAddr, uint64_t toAddr,
                                   BlockTrackingSparseBitmapAccessExtentCB cb,
                                   void *cbData);

// swap two bitmaps
BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_Swap(BlockTrackingSparseBitmap bitmap1,
                               BlockTrackingSparseBitmap bitmap2);

// merge source to destination bitmap
BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_Merge(BlockTrackingSparseBitmap dest,
                                BlockTrackingSparseBitmap src);

// serialize and deserialize
BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_Serialize(BlockTrackingSparseBitmap bitmap,
                                    char *flatBitmap, uint32_t flatBitmapLen);

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_Deserialize(BlockTrackingSparseBitmap bitmap,
                                      const char *flatBitmap,
                                      uint32_t flatBitmapLen);

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_GetStreamMaxSize(uint64_t maxAddr,
                                           uint32_t *streamLen);

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_GetStreamSize(BlockTrackingSparseBitmap bitmap,
                                        uint32_t *streamLen);

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_GetBitCount(BlockTrackingSparseBitmap bitmap,
                                      uint32_t *bitCount);

BlockTrackingSparseBitmapError
BlockTrackingSparseBitmap_GetMemoryInUse(BlockTrackingSparseBitmap bitmap,
                                         uint32_t *memoyInUse);

#endif
