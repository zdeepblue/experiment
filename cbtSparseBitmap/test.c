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

typedef struct {
   uint64_t *_expAddrs;
   uint32_t _expAddrsLen;
   uint32_t _curIndex;
} CheckBitsData;

Bool checkBit(void *data, uint64_t addr)
{
   CheckBitsData *exp = (CheckBitsData *)data;
   //printf("addr = 0x%x\n", addr);
   return (exp->_curIndex < exp->_expAddrsLen) &&
          (addr == exp->_expAddrs[exp->_curIndex++]);
}

void checkBits(BlockTrackingSparseBitmap bitmap, uint64_t exp[], uint32_t expLen)
{
   BlockTrackingSparseBitmapError error;
   CheckBitsData expData = {exp, expLen, 0};
   error = BlockTrackingSparseBitmap_Traverse(bitmap, 0, -1, checkBit, &expData);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   assert(expData._curIndex == expData._expAddrsLen);
}

void testBasic()
{
   Bool isSet = FALSE;
   BlockTrackingSparseBitmap bitmap;
   BlockTrackingSparseBitmapError error;
   uint64_t *expAddrs;
   uint16_t i;
   uint64_t addr;

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

   expAddrs = (uint64_t *)malloc((MAX_ADDR+1) * sizeof(uint64_t));
   assert(expAddrs != NULL);
   expAddrs[0] = 100;
   checkBits(bitmap, expAddrs, 1);

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

   expAddrs[1] = MAX_ADDR;
   checkBits(bitmap, expAddrs, 2);


   // set in one leaf
   error = BlockTrackingSparseBitmap_SetInRange(bitmap, 0xCCCC, 0xCCFD);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   printBitmapStat(bitmap, 512+256, 52);

   // set cross leaves
   error = BlockTrackingSparseBitmap_SetInRange(bitmap, 0xCCFF, 0xCE52);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);
   printBitmapStat(bitmap, 512+256+64, 0x188);

   expAddrs[0] = 100;
   for (i = 1, addr = 0xCCCC; addr <= 0xCCFD ; ++i, ++addr) {
      expAddrs[i] = addr;
   }
   for (addr = 0xCCFF; addr <= 0xCE52; ++addr, ++i) {
      expAddrs[i] = addr;
   }
   expAddrs[i++] = MAX_ADDR;
   checkBits(bitmap, expAddrs, i);

   // set all bits
   error = BlockTrackingSparseBitmap_SetInRange(bitmap, 0, -1);
   assert(error == BLOCKTRACKING_BMAP_ERR_OK);

   printBitmapStat(bitmap, 0x249280, MAX_ADDR+1);

   for (addr = 0; addr <= MAX_ADDR ; ++addr) {
      expAddrs[addr] = addr;
   }
   checkBits(bitmap, expAddrs, addr);

   // destroy
   BlockTrackingSparseBitmap_Destroy(bitmap);
   free(expAddrs);
}

void testMerge()
{
   Bool isSet = FALSE;
   BlockTrackingSparseBitmap bitmap1, bitmap2, bitmap3;
   BlockTrackingSparseBitmapError error;
   uint64_t expAddrs[3];

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

   expAddrs[0] = 0xFF;
   expAddrs[1] = 0x1FF; 
   checkBits(bitmap1, expAddrs, 2);

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

   expAddrs[2] = 0x2FF; 
   checkBits(bitmap1, expAddrs, 3);

   // destroy
   BlockTrackingSparseBitmap_Destroy(bitmap3);
   BlockTrackingSparseBitmap_Destroy(bitmap2);
   BlockTrackingSparseBitmap_Destroy(bitmap1);
}

void testSerialize()
{
   Bool isSet = FALSE;
   char *stream;
   uint32_t streamLen;
   BlockTrackingSparseBitmap bitmap1, bitmap2;
   BlockTrackingSparseBitmapError error;
   uint64_t expAddrs[4];

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

   expAddrs[0] = 0xFF; 
   expAddrs[1] = 0x46C; 
   expAddrs[2] = 0xFFF; 
   expAddrs[3] = 0x1FFFF; 
   checkBits(bitmap2, expAddrs, 4);
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
