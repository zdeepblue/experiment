#ifndef _CBT_SPARSE_BITMAP_H_
#define _CBT_SPARSE_BITMAP_H_

typedef Bool (*BlockTrackingSparseBitmapAccessBitCB)(void *cbData, uint64 addr);

struct BlockTrackingSparseBitmap_t;
typedef struct BlockTrackingSparseBitmap_t *BlockTrackingSparseBitmap;
  
enum BlockTrackingSparseBitmapErrorCode{
   BLOCKTRACKING_BMAP_ERR_OK = 0,
   BLOCKTRACKING_BMAP_ERR_INVALID_ARG,
   BLOCKTRACKING_BMAP_ERR_OUT_OF_RANGE,
   BLOCKTRACKING_BMAP_ERR_OUT_OF_MEM,
   BLOCKTRACKING_BMAP_ERR_FAIL
};

// constructor and destructor
BlockTrackingSparseBitmapErrorCode
BlockTrackingSparseBitmap_Create(BlockTrackingSparseBitmap *bitmap);

void
BlockTrackingSparseBitmap_Destroy(BlockTrackingSparseBitmap bitmap);

// set and query
BlockTrackingSparseBitmapErrorCode
BlockTrackingSparseBitmap_SetAt(BlockTrackingSparseBitmap bitmap, uint64 addr,
                                Bool *oldValue)

BlockTrackingSparseBitmapErrorCode
BlockTrackingSparseBitmap_SetInRange(BlockTrackingSparseBitmap bitmap,
                                     uint64 fromAddr, uint64 toAddr);

BlockTrackingSparseBitmapErrorCode
BlockTrackingSparseBitmap_IsSet(BlockTrackingSparseBitmap bitmap, uint64 addr,
                                Bool *oldValue);

// traverse bits
BlockTrackingSparseBitmapErrorCode
BlockTrackingSparseBitmap_Traverse(BlockTrackingSparseBitmap bitmap,
                                   uint64 fromAddr, uint64 toAddr,
                                   BlockTrackingSparseBitmapAccessBitCB cb,
                                   void *cbData);

// swap two bitmaps
void
BlockTrackingSparseBitmap_Swap(BlockTrackingSparseBitmap bitmap1,
                               BlockTrackingSparseBitmap bitmap2);

// merge source to destination bitmap
BlockTrackingSparseBitmapErrorCode
BlockTrackingSparseBitmap_Merge(BlockTrackingSparseBitmap dest,
                                BlockTrackingSparseBitmap src);

// serialize and deserialize
BlockTrackingSparseBitmapErrorCode
BlockTrackingSparseBitmap_Serialize(BlockTrackingSparseBitmap bitmap,
                                    char *flatBitmap, uint32 flatBitmapLen);

BlockTrackingSparseBitmapErrorCode
BlockTrackingSparseBitmap_Deserialize(BlockTrackingSparseBitmap bitmap,
                                      const char *flatBitmap,
                                      uint32 flatBitmapLen);



#endif
