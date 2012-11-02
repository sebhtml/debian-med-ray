/*
 	Ray
    Copyright (C) 2011  Sébastien Boisvert

	http://DeNovoAssembler.SourceForge.Net/

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, version 3 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You have received a copy of the GNU Lesser General Public License
    along with this program (lgpl-3.0.txt).  
	see <http://www.gnu.org/licenses/>
*/

/* TODO:  replace calls to Malloc by a single call to Malloc */
/* TODO: replace m_allocatedSizes with a bitmap */

/* run low-level assertions, pretty slow but that helped for the development. */
/* even if ASSERT is defined, the low-level assertions are not */
/* compiled unless LOW_LEVEL_ASSERT is defined */

/* #define LOW_LEVEL_ASSERT */

#include <memory/DefragmentationGroup.h>
#include <memory/allocator.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
using namespace std;

#ifdef  ASSERT
#include <assert.h>
#endif

/** bit encoding  in the bit array */
#define AVAILABLE 0
#define UTILISED 1

/** value for an occupied chunk in the array */
#define BUSY_CHUNK 18446744073709551615UL


/**
 *  Return true if the DefragmentationGroup can allocate n elements 
 *  Time complexity: O(1)
 */
bool DefragmentationGroup::canAllocate(int n){
	/* we want fast allocation in the end...  
	if a contiguous segment is not available, we can't handle it... 
*/
	#ifdef ASSERT
	assert((ELEMENTS_PER_GROUP-m_freeSliceStart)<=m_availableElements);
	#endif

	return (ELEMENTS_PER_GROUP-m_freeSliceStart)>=n;
}

/**
 *    fragmented slice             free slice
 * |---------------------------|---------------|
 *
 *                             ^               ^
 *                             |               |
 * 0                           |            ELEMENTS_PER_GROUP-1
 *                             |
 *                     m_freeSliceStart
 *
 *  fragmented slice:  
 *
 *      part of m_block that will undergo fragmentation and defragmentation
 *
 *  free slice:
 *      part of m_block that has no fragmentation
 *      free slice has (ELEMENTS_PER_GROUP-m_freeSliceStart) consecutive AVAILABLE elements
 *      starting at m_freeSliceStart
 *
 *  m_freeSliceStart:
 *      indicates where the free slice begins
 *      
 *      Allocation algorithm:
 *
 * First, we try to find n consecutive AVAILABLE elements in the
 * fragmented slice.
 * This is O(ELEMENTS_PER_GROUP), and is a read-only operation, thus pretty fast.
 *
 * 64-element chunks that are filled completely are just not investigated.
 * 
 * This will defragment the fragmented slice and make the free slice
 * larger.
 * AVAILABLE elements.
 *
 *
 * At first, we must obtain a free handle.
 * This is usually O(256) if there is someone in m_fastPointers available.
 * Otherwise, populating m_fastPointers is O(ELEMENTS_PER_GROUP)
 *
 */
SmallSmartPointer DefragmentationGroup::allocate(int n,
	int bytesPerElement,uint16_t*cellContents,uint8_t*cellOccupancies){

	m_operations[__OPERATION_ALLOCATE]++;

	/* verify pre-conditions */
	#ifdef ASSERT
	assert(n>0);
	assert(canAllocate(n));
	assert(n<=ELEMENTS_PER_GROUP);
	assert(n<=m_availableElements);
	assert(n<=(ELEMENTS_PER_GROUP-m_freeSliceStart));
	assert(m_allocatedOffsets!=NULL);
	assert(m_allocatedSizes!=NULL);
	#endif

/** 
 * get an handle 
 * This is O(256) if an available handle is found in m_fastPointers
 * Otherwise, it is O(65536) -- but the worst case is actually very rare.
 * */
	SmallSmartPointer returnValue=getAvailableSmallSmartPointer();

	/* make sure that this handle is not used already */
	#ifdef ASSERT
	assert(m_allocatedSizes[returnValue]==0);
	#endif

	/** save meta-data for the SmallSmartPointer */
	m_allocatedOffsets[returnValue]=m_freeSliceStart;
	m_allocatedSizes[returnValue]=n;

	/** make sure that the meta-data was stored correctly */
	#ifdef ASSERT
	assert(m_allocatedSizes[returnValue]==n);
	assert(m_allocatedOffsets[returnValue]==m_freeSliceStart);
	#endif

	/** move the frontier */
	m_freeSliceStart+=n;

	/** assert that all things on the right of the frontier **/
	/* are freed */
	#ifdef LOW_LEVEL_ASSERT
	for(int i=m_freeSliceStart;i<ELEMENTS_PER_GROUP;i++){
		if(getBit(i)!=AVAILABLE){
			cout<<"Error, offset "<<i<<" is not AVAILABLE, allocated "<<n<<" at offset "<<offset<<"  m_freeSliceStart="<<m_freeSliceStart<<endl;
			print();
		}
		assert(getBit(i)==AVAILABLE);
	}
	#endif

	/* since we allocated n elements */
	/* we have n less elements left */
	m_availableElements-=n;
	
	// make sure that the number of available elements
	// is valid
	#ifdef ASSERT
	if(m_availableElements<0)
		cout<<"m_availableElements "<<m_availableElements<<endl;
	assert(m_availableElements>=0);
	assert(m_availableElements<=ELEMENTS_PER_GROUP);
	#endif

	__triggerDefragmentationRoutine(bytesPerElement,cellContents,cellOccupancies);

	// finally return safely the handle
	return returnValue;
}

/**
 * Usually very fast, getAvailableSmallSmartPointer returns a SmallSmartPointer
 * that is available. To do so, the list m_fastPointers is searched.
 * This list contains FAST_POINTERS entries (default is 256).
 * If there is a hit, it is returned to the caller.
 * 
 * Otherwise, all the possible SmallSmartPointer handles are probed
 * (there are ELEMENTS_PER_GROUP (default is 65536) of them)
 * and those available are appended to m_fastPointers.
 * 
 * Finally, the SmallSmartPointer that was the last to be recorded to m_fastPointers
 * is returned.
 *
 * Time complexity: O(256) if an handle is available in m_fastPointers
 * O(ELEMENTS_PER_GROUP) otherwise
 */
SmallSmartPointer DefragmentationGroup::getAvailableSmallSmartPointer(){
	/* try to find a handle in the fast pointers */
	/* O(FAST_POINTERS), FAST_POINTERS = 256 I think */
	for(int i=0;i<FAST_POINTERS;i++)
		if(m_allocatedSizes[m_fastPointers[i]]==0) /** it is available ! */
			return m_fastPointers[i];

	/**
 * 	Otherwise, populate m_fastPointers
 *  find an alternative available handle */
	/** O(65536) */

	SmallSmartPointer returnValue=0;

	// populate fast pointers
	// purge fast pointers that are allocated
	int fast=0;
	while(m_allocatedSizes[m_fastPointers[fast]]==0 && fast<FAST_POINTERS)
		fast++;

	int i=0;

	// populate fast pointers
	// with available handles
	// O(ELEMENTS_PER_GROUP), ELEMENTS_PER_GROUP= 65536
	for(i=0;i<ELEMENTS_PER_GROUP;i++){
		if(m_allocatedSizes[i]==0){
			m_fastPointers[fast++]=i;
			/* always update returnValue to return the last one observed */
			/* otherwise, if the first one is returned, it will be useless */
			/* as a fast pointer */

			/* TODO: possibly set returnValue to a handle that is not a fast pointer if possible */
			
			returnValue=i;
		}

		// add the fast pointer
		while(m_allocatedSizes[m_fastPointers[fast]]==0 && fast<FAST_POINTERS)
			fast++;

		/** we populated m_fastPointers */
		/** we can stop now **/
		if(fast==FAST_POINTERS)
			break;
	}

	/* return a handle */
	return returnValue;
}

/**
 * deallocate a SmallSmartPointer
 * defragment
 * done.
 *
 * Time complexity: O(n) when< CONFIG_DEFRAGMENTATION_THRESHOLD elements are in the fragmented zone
 * Otherwise, O(ELEMENTS_PER_GROUP) for the call to defragment()
 */
void DefragmentationGroup::deallocate(SmallSmartPointer a,int bytesPerElement,uint16_t*cellContent,
	uint8_t*cellOccupancies){

	m_operations[__OPERATION_DEALLOCATE]++;

	#ifdef LOW_LEVEL_ASSERT
	assert((ELEMENTS_PER_GROUP-m_freeSliceStart)<=m_availableElements);
	for(int i=m_freeSliceStart;i<ELEMENTS_PER_GROUP;i++){
		if(getBit(i)!=AVAILABLE){
			cout<<"Error, offset "<<i<<" is not AVAILABLE"<<endl;
			print();
		}
		assert(getBit(i)==AVAILABLE);
	}
	#endif

	#ifdef ASSERT
	if(m_allocatedSizes[a]==0)
		cout<<__func__<<" SmallSmartPointer "<<(int)a<<" has size 0."<<endl;
	assert(m_allocatedSizes[a]>0);
	#endif

	// fetch meta-data
	int allocatedSize=m_allocatedSizes[a];
	int offset=m_allocatedOffsets[a];

	/** set the size to 0 for this SmallSmartPointer */
	m_allocatedSizes[a]=0;

	// this is a special case
	/* in which the region touches the frontier */
	if(offset+allocatedSize==m_freeSliceStart){
		m_freeSliceStart=offset;
	}

	#ifdef LOW_LEVEL_ASSERT
	for(int i=m_freeSliceStart;i<ELEMENTS_PER_GROUP;i++){
		if(getBit(i)!=AVAILABLE){
			cout<<"Error, offset "<<i<<" is not AVAILABLE"<<endl;
			print();
		}
		assert(getBit(i)==AVAILABLE);
	}
	#endif

	#ifdef LOW_LEVEL_ASSERT
	assert(m_allocatedSizes[a]==0);
	#endif
	
	/* we have more elements available now */
	m_availableElements+=allocatedSize;

	#ifdef ASSERT
	assert(m_availableElements<=ELEMENTS_PER_GROUP);
	assert(m_availableElements>=0);
	if(!((ELEMENTS_PER_GROUP-m_freeSliceStart)<=m_availableElements))
		cout<<"elements in freeSlice: "<<ELEMENTS_PER_GROUP-m_freeSliceStart<<" available "<<m_availableElements<<" m_freeSliceStart "<<m_freeSliceStart<<endl;
	assert((ELEMENTS_PER_GROUP-m_freeSliceStart)<=m_availableElements);
	#endif

	__triggerDefragmentationRoutine(bytesPerElement,cellContent,cellOccupancies);
}

/**
 * Kick-start a DefragmentationGroup.
 *
 * Time complexity: O(1)
 */
void DefragmentationGroup::constructor(int bytesPerElement,bool show){

	m_operations[__OPERATION_ALLOCATE]=0;
	m_operations[__OPERATION_DEALLOCATE]=0;
	m_operations[__OPERATION_DEFRAGMENT]=0;
	
	#ifdef ASSERT
	assert(m_block==NULL);
	#endif

	m_availableElements=ELEMENTS_PER_GROUP;
	
	#ifdef ASSERT
	assert(m_availableElements<=ELEMENTS_PER_GROUP);
	assert(m_availableElements>=0);
	#endif
	
	for(int i=0;i<FAST_POINTERS;i++)
		m_fastPointers[i]=i;

	m_freeSliceStart=0;
	/** allocate the memory */
	m_block=(uint8_t*)__Malloc(ELEMENTS_PER_GROUP*bytesPerElement,"RAY_MALLOC_TYPE_DEFRAG_GROUP",show);

	/** initialise sizes */
	m_allocatedSizes=(uint8_t*)__Malloc(ELEMENTS_PER_GROUP*sizeof(uint8_t),"RAY_MALLOC_TYPE_DEFRAG_GROUP",show);
	m_allocatedOffsets=(uint16_t*)__Malloc(ELEMENTS_PER_GROUP*sizeof(uint16_t),"RAY_MALLOC_TYPE_DEFRAG_GROUP",show);
	
	for(int i=0;i<ELEMENTS_PER_GROUP;i++){
		m_allocatedSizes[i]=0;
		m_allocatedOffsets[i]=0;
	}

	#ifdef ASSERT
	assert((ELEMENTS_PER_GROUP-m_freeSliceStart)<=m_availableElements);
	#endif
}

/**
 * Free memory
 */
void DefragmentationGroup::destructor(bool show){
	__Free(m_block,"RAY_MALLOC_TYPE_DEFRAG_GROUP",show);
	m_block=NULL;
	__Free(m_allocatedSizes,"RAY_MALLOC_TYPE_DEFRAG_GROUP",show);
	m_allocatedSizes=NULL;
	__Free(m_allocatedOffsets,"RAY_MALLOC_TYPE_DEFRAG_GROUP",show);
	m_allocatedOffsets=NULL;
}

/**
 * Resolvee a SmallSmartPointer
 */
void*DefragmentationGroup::getPointer(SmallSmartPointer a,int bytesPerElement){
	#ifdef ASSERT
	if(m_allocatedSizes[a]==0)
		cout<<"this= "<<this<<" can not getPointer on SmallSmartPointer "<<(int)a<<" because it is not allocated."<<endl;
	assert(m_allocatedSizes[a]!=0);
	#endif
	int offset=m_allocatedOffsets[a];
	void*pointer=m_block+offset*bytesPerElement;
	return pointer;
}

/**
 * Set pointers to NULL
 */
void DefragmentationGroup::setPointers(){
	m_block=NULL;
	m_allocatedOffsets=NULL;
	m_allocatedSizes=NULL;
}

/**
 * Check if the DefragmentationGroup is active.
 */
bool DefragmentationGroup::isOnline(){
	return m_block!=NULL;
}

/**
 * Get the number of available elements
 */
int DefragmentationGroup::getAvailableElements(){
	#ifdef ASSERT
	assert(m_block!=NULL);
	assert(m_availableElements<=ELEMENTS_PER_GROUP);
	assert(m_availableElements>=0);
	#endif
	return m_availableElements;
}

void DefragmentationGroup::print(){
}

int DefragmentationGroup::getFreeSliceStart(){
	return m_freeSliceStart;
}

/**
 * defragmentation is O(ELEMENTS_PER_GROUP)
 * there is a frontier called m_freeSliceStart
 * on the left, things are fragmented
 * on the right, things are free and nothing is allocated
 * What this code does is just linearly compact the gaps
 * from left to right
 * while doing so, meta-data
 * are moved correctly.
 * \author Sébastien Boisvert
 */

#define __defragment_optimized
bool DefragmentationGroup::defragment(int bytesPerElement,uint16_t*cellContents,uint8_t*cellOccupancies){


	m_operations[__OPERATION_DEFRAGMENT]++;

	#ifdef __defragment_optimized

	memset(cellOccupancies,0x0,ELEMENTS_PER_GROUP);

	#else

	/* update the look-up table */
	for(int i=0;i<ELEMENTS_PER_GROUP;i++){
		cellOccupancies[i]=0;
	}

	#endif

	
	for(int i=0;i<ELEMENTS_PER_GROUP;i++){
		/* otherwise we don't want to register it because it may overwrite someone else */
		if(m_allocatedSizes[i]!=0){
			cellContents[m_allocatedOffsets[i]]=i;
			for(int j=0;j<m_allocatedSizes[i];j++)
				cellOccupancies[m_allocatedOffsets[i]+j]=1;
		}
	}

	#ifdef ASSERT
	assert((ELEMENTS_PER_GROUP-m_freeSliceStart)<=m_availableElements);
	#endif

	/* nothing is available */
	if(m_availableElements==0)
		return false;

	/* everything is already in the free slice */
	int elementsInFreeSlice=ELEMENTS_PER_GROUP-m_freeSliceStart;
	if(m_availableElements==elementsInFreeSlice)
		return false;

	int destination=0;
	int source=0;

	// skip allocated stuff.
	while(cellOccupancies[destination]==1 && destination<ELEMENTS_PER_GROUP)
		destination++;

	source=destination+1;

	while(cellOccupancies[source]==0 && source<ELEMENTS_PER_GROUP)
		source++;

	while(source<ELEMENTS_PER_GROUP){
		/* if source is empty, we are done. */
		if(cellOccupancies[source]==0)
			break;

		/* at this point, source points to a offset where a SmallSmartPointer starts */
		/* and destination is an empty space. */
		#ifdef ASSERT
		if(!(source>destination)){
			cout<<"destination "<<destination<<" source "<<source<<endl;
			print();
		}
		assert(source>destination);
		#endif

		#ifdef ASSERT
		assert(cellOccupancies[destination]==0);
		assert(cellOccupancies[source]==1);
		#endif

		/* here we copy source in destination */
		/* copy the data */
		SmallSmartPointer smartPointer=cellContents[source];

		#ifdef ASSERT
		assert(cellOccupancies[source-1]==0);
		if(!(m_allocatedSizes[smartPointer]!=0)){
			cout<<"SmallSmartPointer "<<smartPointer<<" must move, but is has size 0 and bit is 1"<<endl;
			print();
		}
		assert(m_allocatedSizes[smartPointer]!=0);
		#endif
		
		int n=m_allocatedSizes[smartPointer];
		for(int i=0;i<n;i++){
			for(int byte=0;byte<bytesPerElement;byte++){
				m_block[(destination+i)*bytesPerElement+byte]=m_block[(source+i)*bytesPerElement+byte];
			
			}
		}

		#ifdef __defragment_optimized

		if(n>0)
			memset(cellOccupancies+destination,0x1,n);
		
		int count=source-destination;

		if(count>0)
			memset(cellOccupancies+destination+n,0x0,count);

		#else

		for(int i=destination;i<destination+n;i++)
			cellOccupancies[i]=1;

		for(int i=destination+n;i<source+n;i++)
			cellOccupancies[i]=0;

		#endif

		m_allocatedOffsets[smartPointer]=destination;

		/* move destination after the new location of smartPointer */
		destination+=n;
		source+=n;

		/* skip occupied elements */
		while(cellOccupancies[destination]==1 && destination<ELEMENTS_PER_GROUP)
			destination++;

		/* now destination is at a free block */

		/* skip available elements */
		while(cellOccupancies[source]==0 && source<ELEMENTS_PER_GROUP)
			source++;
	}
	
	m_freeSliceStart=destination;

	#ifdef ASSERT
	assert((ELEMENTS_PER_GROUP-m_freeSliceStart)<=m_availableElements);
	#endif

	#ifdef LOW_LEVEL_ASSERT
	for(int i=m_freeSliceStart;i<ELEMENTS_PER_GROUP;i++)
		assert(getBit(i)==AVAILABLE);

	for(int i=0;i<m_freeSliceStart;i++)
		assert(getBit(i)==UTILISED);

	int bitTo1=0;
	int elements=0;
	for(int i=0;i<ELEMENTS_PER_GROUP;i++){
		bitTo1+=getBit(i);
		if(m_allocatedSizes[i]!=0){
			if(!(m_allocatedOffsets[i]<m_freeSliceStart)){
				cout<<"SmallSmartPointer "<<i<<" has offset is "<<m_allocatedOffsets[i]<<" but m_freeSliceStart is "<<m_freeSliceStart<<" size: "<<(int)m_allocatedSizes[i]<<endl;
				print();
			}
			assert(m_allocatedOffsets[i]<m_freeSliceStart);
			elements+=m_allocatedSizes[i];
			for(int j=0;j<m_allocatedSizes[i];j++){
				if(!(getBit(m_allocatedOffsets[i]+j)==UTILISED)){
					cout<<"Error, SmallSmartPointer "<<i<<" offset "<<(int)m_allocatedOffsets[i]<<" size "<<(int)m_allocatedSizes[i]<<" is not in the bitmap at j="<<j<<endl;
					print();
				}
				assert(getBit(m_allocatedOffsets[i]+j)==UTILISED);
			}
		}
	}
	assert(bitTo1==elements);
	#endif

	return true;
}

#undef __defragment_optimized

void DefragmentationGroup::__triggerDefragmentationRoutine(int bytesPerElement,uint16_t*cellContents,
	uint8_t*cellOccupancies){

	int elementsInFreeSlice=getContiguousElements();
	int fragmentedElements=getFragmentedElements();

	/** this threshold is the minimum number of bins in the fragmented zone
 * 	that are necessary to trigger a defragmentation 
 *	low value will trigger a lot of defragmentation events, but defragmentation
 *	is kind of slow. Therefore, you want a amortized time complexity -- that is
 *	in general we don't defragment (fast), but sometimes we do (slow)
 *	many fast events plus a few slow events is hopefully more fast than slow.
 *
 *	\date 2012-08-09 if there are still some good stuff
 * 	*/

	/** the main client of this code is the hash table */
	/** the hash table allocates a maximum of 64 elements for a single call of allocate() **/

	int thresholdForFragmentedWarZone=2048;
	int thresholdForFreeSlice=128;

	if(fragmentedElements>= thresholdForFragmentedWarZone
		&& elementsInFreeSlice <= thresholdForFreeSlice )
		defragment(bytesPerElement,cellContents,cellOccupancies);

}

int DefragmentationGroup::getFragmentedElements(){

	int elementsInFreeSlice=getContiguousElements();
	int fragmentedElements=m_availableElements-elementsInFreeSlice;

	return fragmentedElements;
}

int DefragmentationGroup::getContiguousElements(){

	int elementsInFreeSlice=ELEMENTS_PER_GROUP-m_freeSliceStart;

	return elementsInFreeSlice;
}

int DefragmentationGroup::getOperations(int operationCode){
	return m_operations[operationCode];
}
