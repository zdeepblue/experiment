#include "cbtSparseBitmap.h"

#include <stddef.h>
#include <assert.h>
#include <stdio.h>

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
int main()
{
   Bool isSet = FALSE;
   BlockTrackingSparseBitmap bitmap;
   BlockTrackingSparseBitmapError error;

   // create
   error = BlockTrackingSparseBitmap_Create(&bitmap);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   // set and query normal
   error = BlockTrackingSparseBitmap_SetAt(bitmap, 100, NULL);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   error = BlockTrackingSparseBitmap_IsSet(bitmap, 100, &isSet);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(isSet);

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

   printf("All test passed.\n");
}
