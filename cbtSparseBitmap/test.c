#include "cbtSparseBitmap.h"

#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_ADDR 0xFFFFFFull


void printBitmapStat(BlockTrackingSparseBitmap bitmap,
                     uint32_t expMem, uint32_t expBits)
{
   BlockTrackingSparseBitmapError error;
   uint32_t memoryInUse, bitCount;

   error = BlockTrackingSparseBitmap_GetMemoryInUse(bitmap, &memoryInUse);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   printf("memory in use = 0x%x\n", memoryInUse);
   error = BlockTrackingSparseBitmap_GetBitCount(bitmap, &bitCount);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   printf("bit count = 0x%x\n", bitCount);

   assert(expMem == memoryInUse);
   assert(expBits == bitCount);
}

void testBasic()
{
   Bool isSet = FALSE;
   BlockTrackingSparseBitmap bitmap;
   BlockTrackingSparseBitmapError error;

   printf("=== %s === \n", __FUNCTION__);
   // create
   error = BlockTrackingSparseBitmap_Create(&bitmap);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   // set and query normal
   error = BlockTrackingSparseBitmap_SetAt(bitmap, 100, NULL);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   error = BlockTrackingSparseBitmap_IsSet(bitmap, 100, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(isSet);

   isSet = FALSE;
   error = BlockTrackingSparseBitmap_SetAt(bitmap, 100, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(isSet);

   error = BlockTrackingSparseBitmap_IsSet(bitmap, 101, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(!isSet);

   printBitmapStat(bitmap, 128, 1);
   // set on max
   error = BlockTrackingSparseBitmap_SetAt(bitmap, MAX_ADDR, NULL);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   isSet = FALSE;
   error = BlockTrackingSparseBitmap_IsSet(bitmap, MAX_ADDR, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(isSet);

   printBitmapStat(bitmap, 512, 2);
   // set on max+1
   error = BlockTrackingSparseBitmap_SetAt(bitmap, MAX_ADDR+1, NULL);
   assert(error == BLOCKTRACKING_BMAP_ERR_INVALID_ADDR);

   printBitmapStat(bitmap, 512, 2);

   // set all bits
   error = BlockTrackingSparseBitmap_SetInRange(bitmap, 0, -1);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   printBitmapStat(bitmap, 0x249280, 1u << 24);

   // destroy
   BlockTrackingSparseBitmap_Destroy(bitmap);

}

void testMerge()
{
   Bool isSet = FALSE;
   BlockTrackingSparseBitmap bitmap1, bitmap2, bitmap3;
   BlockTrackingSparseBitmapError error;

   printf("=== %s === \n", __FUNCTION__);
   // create
   error = BlockTrackingSparseBitmap_Create(&bitmap1);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   error = BlockTrackingSparseBitmap_Create(&bitmap2);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   // at same leaf
   error = BlockTrackingSparseBitmap_SetAt(bitmap1, 0xFF, NULL);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   error = BlockTrackingSparseBitmap_SetAt(bitmap2, 0x1FF, NULL);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   error = BlockTrackingSparseBitmap_Merge(bitmap1, bitmap2);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   error = BlockTrackingSparseBitmap_IsSet(bitmap1, 0xFF, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(isSet);
   isSet = FALSE;
   error = BlockTrackingSparseBitmap_IsSet(bitmap1, 0x1FF, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(isSet);
   printBitmapStat(bitmap1, 128, 2);

   // diff leaf
   error = BlockTrackingSparseBitmap_Create(&bitmap3);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   isSet = FALSE;
   error = BlockTrackingSparseBitmap_SetAt(bitmap3, 0x2FF, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(!isSet);

   error = BlockTrackingSparseBitmap_Merge(bitmap1, bitmap3);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   error = BlockTrackingSparseBitmap_IsSet(bitmap1, 0x2FF, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(isSet);

   printBitmapStat(bitmap1, 256, 3);

   // destroy
   BlockTrackingSparseBitmap_Destroy(bitmap1);
}

void testSerialize()
{
   Bool isSet = FALSE;
   char *stream;
   uint32_t streamLen;
   BlockTrackingSparseBitmap bitmap1, bitmap2;
   BlockTrackingSparseBitmapError error;

   printf("=== %s === \n", __FUNCTION__);
   // create
   error = BlockTrackingSparseBitmap_Create(&bitmap1);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   error = BlockTrackingSparseBitmap_Create(&bitmap2);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   isSet = TRUE;
   error = BlockTrackingSparseBitmap_SetAt(bitmap1, 0xFF, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(!isSet);
   isSet = TRUE;
   error = BlockTrackingSparseBitmap_SetAt(bitmap1, 0xFFF, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(!isSet);
   isSet = TRUE;
   error = BlockTrackingSparseBitmap_SetAt(bitmap1, 0x1FFFF, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(!isSet);
   printBitmapStat(bitmap1, 512, 3);

   error = BlockTrackingSparseBitmap_GetStreamSize(bitmap1, &streamLen);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   stream = (char *)calloc(streamLen, 1);
   error = BlockTrackingSparseBitmap_Serialize(bitmap1, stream, streamLen);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   isSet = TRUE;
   error = BlockTrackingSparseBitmap_SetAt(bitmap2, 0x46C, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(!isSet);

   error = BlockTrackingSparseBitmap_Deserialize(bitmap2, stream, streamLen);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   isSet = FALSE;
   error = BlockTrackingSparseBitmap_IsSet(bitmap2, 0xFF, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(isSet);
   isSet = FALSE;
   error = BlockTrackingSparseBitmap_IsSet(bitmap2, 0xFFF, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(isSet);
   isSet = FALSE;
   error = BlockTrackingSparseBitmap_IsSet(bitmap2, 0x1FFFF, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(isSet);
   isSet = FALSE;
   error = BlockTrackingSparseBitmap_IsSet(bitmap2, 0x46C, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(isSet);
   printBitmapStat(bitmap2, 576, 4);

   // destroy
   BlockTrackingSparseBitmap_Destroy(bitmap2);
   BlockTrackingSparseBitmap_Destroy(bitmap1);
}

int main()
{
   testBasic();
   testMerge();
   testSerialize();

   printf("All test cases passed.\n");
}
