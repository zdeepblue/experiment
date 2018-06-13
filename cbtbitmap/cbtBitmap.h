/* ***************************************************************************
 * Copyright 2018 VMware, Inc.  All rights reserved.
 * -- VMware Confidential
 * **************************************************************************/

/*
 * CBT Bitmap Public APIs
 *
 * CBT Bitmap is consumed by Changed Block Tracking (CBT).
 * Each bit in the bitmap presents a changed block.
 *
 * It supports set bit(s), query, traverse, serialize/deserialize, statistics
 * operations for CBT scenarios.
 */

#ifndef _CBT_BITMAP_H_
#define _CBT_BITMAP_H_

#ifdef CBT_BITMAP_UNITTEST
#include <stdint.h>
#define TRUE 1
#define FALSE 0
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t  uint8;
typedef char     Bool;
#else
#include <vm_basic_types.h>
#endif

typedef Bool (*CBTBitmapAccessBitCB)(void *cbData, uint64 addr);
typedef Bool (*CBTBitmapAccessExtentCB)(void *cbData, uint64 start, uint64 end);

struct CBTBitmap;
typedef struct CBTBitmap *CBTBitmap;

/*
 * The CBT Bitmap Allocator.
 */
typedef struct CBTBitmapAllocator {
   /*
    *--------------------------------------------------------------------------
    *
    * allocator --
    *
    *    Allocate a memory chunk in specific size.
    *
    * Parameter:
    *    data - input/ouput. User specific callback data which is _data field.
    *    size - input. The requested chunk size.
    *
    * Results:
    *    return the allocated memory chunk.
    *
    *--------------------------------------------------------------------------
    */
   void *(*allocate)(void *data, uint64 size);

   /*
    *--------------------------------------------------------------------------
    *
    * deallocate --
    *
    *    Deallocate the memory allocated by allocate call.
    *
    * Parameter:
    *    data - input/ouput. User specific callback data which is _data field.
    *    ptr - input. A pointer to the chunk.
    *
    * Results:
    *    None.
    *
    *--------------------------------------------------------------------------
    */
   void (*deallocate)(void *data, void *ptr);
   void *_data;
} CBTBitmapAllocator;


/*
 * CBT bitmap error code.
 */
typedef enum {
   CBT_BMAP_ERR_OK = 0,
   CBT_BMAP_ERR_INVALID_ARG,
   CBT_BMAP_ERR_INVALID_ADDR,
   CBT_BMAP_ERR_OUT_OF_RANGE,
   CBT_BMAP_ERR_OUT_OF_MEM,
   CBT_BMAP_ERR_REINIT,
   CBT_BMAP_ERR_FAIL
} CBTBitmapError;


/*
 * CBT Bitmap creation flags.
 * The flags can be combined to meet different user specific scenarios.
 */
#define CBT_BMAP_MODE_FAST_SET       1 // count set bit in fast way
#define CBT_BMAP_MODE_FAST_MERGE     2 // merge operation in fast way
#define CBT_BMAP_MODE_FAST_SERIALIZE 4 // serialize operation in fast way
#define CBT_BMAP_MODE_FAST_STATISTIC 8 // statistics in fast way


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_Init --
 *
 *    Initialize CBTBitmap lib which should be called once per process.
 *
 *    If the alloc is specified here, The member fields of the alloc are copied
 *    in global scope of the bitmap lib. So they must be available in the whole
 *    life time of the bitmap lib. In other words, they must be applicable until
 *    CBTBitmap_Exit is called.
 *
 * Parameter:
 *    alloc - optional input.
 *            Specify an allocator. If NULL, it uses a default one.
 *
 * Results:
 *    return error code if cannot init it successfully
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_Init(CBTBitmapAllocator *alloc);


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_Exit --
 *
 *    Exit CBTBitmap lib which should be called once only per process, as a pair
 *    of CBTBitmap_Init.
 *
 * Results:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */
void
CBTBitmap_Exit();


///////////////////////////////////////////////////////////////////////////////
//    constructor and destructor
///////////////////////////////////////////////////////////////////////////////


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_Create --
 *
 *    Create a bitmap instance with expected creation mode.
 *
 * Parameter:
 *    bitmap - output. A pointer to the bitmap instance.
 *    mode - input. bit-Or flags of CBT_BMAP_MODE_*.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_Create(CBTBitmap *bitmap, uint16 mode);


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_Destroy --
 *
 *    Destroy a bitmap instance created by CBTBitmap_Create
 *
 * Parameter:
 *    bitmap - input. A bitmap instance.
 *
 * Results:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

void
CBTBitmap_Destroy(CBTBitmap bitmap);


///////////////////////////////////////////////////////////////////////////////
//    set and query
///////////////////////////////////////////////////////////////////////////////


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_SetAt --
 *
 *    Set a bit in the bitmap.
 *
 * Parameter:
 *    bitmap - input. A bitmap instance.
 *    addr - input. The address of the bit should be set.
 *    oldValue - output. The original value of the bit being set.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_SetAt(CBTBitmap bitmap, uint64 addr, Bool *oldValue);


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_SetInRange --
 *
 *    Set bits in a range in the bitmap.
 *
 * Parameter:
 *    bitmap - input. A bitmap instance.
 *    fromAddr - input. The beginning of the address of the bit should be set.
 *    toAddr - input. The end of the address of the bit should be set.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_SetInRange(CBTBitmap bitmap, uint64 fromAddr, uint64 toAddr);


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_IsSet --
 *
 *    Check one bit is set or not in the bitmap.
 *
 * Parameter:
 *    bitmap - input. A bitmap instance.
 *    addr - input. The address of the bit is checking.
 *    value - output. The value of the bit.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_IsSet(CBTBitmap bitmap, uint64 addr, Bool *value);


///////////////////////////////////////////////////////////////////////////////
//    traverse
///////////////////////////////////////////////////////////////////////////////


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_TraverseByBit --
 *
 *    Traverse the bitmap by set bit. For each set bit, the callback is called.
 *
 * Parameter:
 *    bitmap - input. A bitmap instance.
 *    fromAddr - input. The beginning of the address of the bit should be set.
 *    toAddr - input. The end of the address of the bit should be set.
 *    cb - input. The callback for each set bit.
 *    cbData - input. The callback data.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_TraverseByBit(CBTBitmap bitmap, uint64 fromAddr, uint64 toAddr,
                        CBTBitmapAccessBitCB cb, void *cbData);


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_TraverseByExtent --
 *
 *    Traverse the bitmap by extent in which all bits are set.
 *    For each extent, the callback is called.
 *
 * Parameter:
 *    bitmap - input. A bitmap instance.
 *    fromAddr - input. The beginning of the address of the bit should be set.
 *    toAddr - input. The end of the address of the bit should be set.
 *    cb - input. The callback for each extent.
 *    cbData - input. The callback data.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_TraverseByExtent(CBTBitmap bitmap, uint64 fromAddr, uint64 toAddr,
                           CBTBitmapAccessExtentCB cb, void *cbData);


///////////////////////////////////////////////////////////////////////////////
//    operations on two bitmaps
///////////////////////////////////////////////////////////////////////////////


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_Swap --
 *
 *    Swap bitmap1 and bitmap2 quickly without deep copy.
 *
 * Parameter:
 *    bitmap1 - input. A bitmap instance.
 *    bitmap2 - input. A bitmap instance.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_Swap(CBTBitmap bitmap1, CBTBitmap bitmap2);


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_Merge --
 *
 *    Merge all set bits of the source bitmap to the destination bitmap.
 *    The source bitmap is unchanged.
 *
 * Parameter:
 *    dest - input/output. A bitmap instance that merging to.
 *    source - input. A bitmap instance that merging from.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_Merge(CBTBitmap dest, CBTBitmap src);


///////////////////////////////////////////////////////////////////////////////
//    serialize and deserialize
///////////////////////////////////////////////////////////////////////////////


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_Serialize --
 *
 *    Serialize the bitmap into a stream which should only be de-serialized by
 *    CBTBitmap_Deserialize.
 *
 * Parameter:
 *    bitmap - input. A bitmap instance.
 *    stream - output. The output stream of the serialized data.
 *    streamLen - input. The length of the output stream.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_Serialize(CBTBitmap bitmap, char *stream, uint32 streamLen);


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_Deserialize --
 *
 *    Deserialize the input stream and merge the bits in the stream to the
 *    current bitmap.
 *
 * Parameter:
 *    bitmap - input/ouput. A bitmap instance.
 *    stream - input. The input stream of the serialized data.
 *    streamLen - input. The length of the output stream.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_Deserialize(CBTBitmap bitmap, const char *stream, uint32 streamLen);


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_GetStreamMaxSize --
 *
 *    Get the maximum stream length bases on the maximum address which is
 *    presented in a bitmap.
 *
 * Parameter:
 *    maxAddr - input. The maximum address.
 *    streamLen - output. A pointer to the length of the stream.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_GetStreamMaxSize(uint64 maxAddr, uint32 *streamLen);


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_GetStreamSize --
 *
 *    Get the stream size for a bitmap instance.
 *    It should be called to get the proper size of an output stream for
 *    serialization.
 *
 * Parameter:
 *    bitmap - input. A bitmap instance.
 *    streamLen - output. A pointer to the length of the stream.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_GetStreamSize(CBTBitmap bitmap, uint32 *streamLen);


///////////////////////////////////////////////////////////////////////////////
//    Statistics
///////////////////////////////////////////////////////////////////////////////


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_GetBitCount --
 *
 *    Get the count of set-bits in the bitmap.
 *
 * Parameter:
 *    bitmap - input. A bitmap instance.
 *    bitCount - output. A pointer to the set-bit count.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_GetBitCount(CBTBitmap bitmap, uint32 *bitCount);


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_GetMemoryInUse --
 *
 *    Get memory consumption of the bitmap.
 *
 * Parameter:
 *    bitmap - input. A bitmap instance.
 *    memoryInUse - output. A pointer to the memory in use.
 *
 * Results:
 *    error code.
 *
 *-----------------------------------------------------------------------------
 */

CBTBitmapError
CBTBitmap_GetMemoryInUse(CBTBitmap bitmap, uint32 *memoyInUse);


/*
 *-----------------------------------------------------------------------------
 *
 * CBTBitmap_GetCapacity --
 *
 *    Get capacity of CBT bitmap
 *
 * Results:
 *    The capacity
 *
 *-----------------------------------------------------------------------------
  */

uint64
CBTBitmap_GetCapacity();

#endif
