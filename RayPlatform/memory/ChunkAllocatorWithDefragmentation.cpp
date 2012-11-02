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

#include <memory/ChunkAllocatorWithDefragmentation.h>
#include <memory/allocator.h>

#ifdef ASSERT
#include <assert.h>
#endif

#include <iostream>
using namespace std;

/** this does nothing
 * defragmentation instead occurs sometimes when
 * deallocate() is called
 */
void ChunkAllocatorWithDefragmentation::defragment(){
}

/**
* print allocator information
*/
void ChunkAllocatorWithDefragmentation::print(){
	cout<<"DefragmentationLane objects: "<<m_numberOfLanes<<endl;

	for(int i=0;i<m_numberOfLanes;i++){
		DefragmentationLane*lane=m_defragmentationLanes[i];
		#ifdef ASSERT
		assert(lane!=NULL);
		#endif
		cout<<"DefragmentationGroup objects in DefragmentationLane # "<<lane->getNumber()<<endl;
		for(int group=0;group<GROUPS_PER_LANE;group++){

			DefragmentationGroup*theGroup=lane->getGroup(group);
			if(theGroup->isOnline()){
				int availableElements=theGroup->getAvailableElements();

				cout<<" # "<<group<<" usage: "<<(ELEMENTS_PER_GROUP-availableElements)<<"/"<<ELEMENTS_PER_GROUP;
				cout<<" FreeSliceStart="<<theGroup->getFreeSliceStart();

				cout<<" Fragmented="<<theGroup->getFragmentedElements();
				cout<<" Contiguous="<<theGroup->getContiguousElements();

				cout<<endl;
				cout<<"   operations: ";
				cout<<" "<<theGroup->getOperations(__OPERATION_ALLOCATE)<<" allocate calls;";
 				cout<<" "<<theGroup->getOperations(__OPERATION_DEALLOCATE)<<" deallocate calls;";
				cout<<" "<<theGroup->getOperations(__OPERATION_DEFRAGMENT)<<" defragment calls"<<endl;

			}
		}
	}
}

/** clear allocations */
void ChunkAllocatorWithDefragmentation::destructor(){
	/** destroy DefragmentationLanes */
	for(int i=0;i<m_numberOfLanes;i++){
		DefragmentationLane*lane=m_defragmentationLanes[i];
		for(int group=0;group<GROUPS_PER_LANE;group++){
			if(lane->getGroup(group)->isOnline())
				lane->getGroup(group)->destructor(m_show);
		}
		__Free(lane,"RAY_MALLOC_TYPE_DEFRAG_LANE",m_show);
		m_defragmentationLanes[i]=NULL;
	}
	__Free(m_cellOccupancies,"RAY_MALLOC_TYPE_DEFRAG_LANE",m_show);
	__Free(m_cellContents,"RAY_MALLOC_TYPE_DEFRAG_LANE",m_show);
	m_cellContents=NULL;
	m_cellOccupancies=NULL;
}

/** constructor almost does nothing  */
void ChunkAllocatorWithDefragmentation::constructor(int bytesPerElement,bool show){
	m_cellContents=(uint16_t*)__Malloc(ELEMENTS_PER_GROUP*sizeof(uint16_t),"RAY_MALLOC_TYPE_DEFRAG_LANE",show);
	m_cellOccupancies=(uint8_t*)__Malloc(ELEMENTS_PER_GROUP*sizeof(uint8_t),"RAY_MALLOC_TYPE_DEFRAG_LANE",show);
	m_show=show;
	m_bytesPerElement=bytesPerElement;

	// initially, the allocator has
	// no line
	for(int i=0;i<NUMBER_OF_LANES;i++)
		m_defragmentationLanes[i]=NULL;

	// furthermore, the fast lane
	// Is not set
	m_fastLane=NULL;
	m_numberOfLanes=0;
}

/**
 * This method updates the fast lane
 * update:
 *  m_fastLane
 *  m_fastGroup
 *  m_fastLaneNumber
 */
void ChunkAllocatorWithDefragmentation::updateFastLane(int n){
	/** find a DefragmentationGroup in a DefragmentationLane that 
 * can accomodate the query */
	for(int i=0;i<m_numberOfLanes;i++){
		DefragmentationLane*lane=m_defragmentationLanes[i];
		if(lane->canAllocate(n,m_bytesPerElement,m_show)){
			m_fastLane=lane;
			return;
		}
	}

	/** we need to add a defragmentation lane because the existing lanes have
 * 	no group that can allocate the query */
	
	DefragmentationLane*defragmentationLane=(DefragmentationLane*)__Malloc(sizeof(DefragmentationLane),"RAY_MALLOC_TYPE_DEFRAG_LANE",m_show);
	defragmentationLane->constructor(m_numberOfLanes,m_bytesPerElement,m_show);

	m_defragmentationLanes[m_numberOfLanes++]=defragmentationLane;

	/** ask the lane if it can allocate */
	if(defragmentationLane->canAllocate(n,m_bytesPerElement,m_show))
		m_fastLane=defragmentationLane;
}

/**
 * allocate memory
 */
SmartPointer ChunkAllocatorWithDefragmentation::allocate(int n){ /** 64 is the number of buckets in a MyHashTableGroup */

	// presently, this code only allocate things between 1 and 64 * sizeof(something)
	#ifdef ASSERT
	if(!(n>=1 && n<=64))
		cout<<"n= "<<n<<endl;
	assert(n>=1&&n<=64);
	#endif

	// we need to update the fast lane
	if(m_fastLane==NULL || !m_fastLane->canAllocate(n,m_bytesPerElement,m_show)){
		updateFastLane(n);
	}

	// the fast lane gives us a smart pointer
	// a small smart pointer can only be resolved with the context of
	// a DefragmentationGroup
	int group;
	SmallSmartPointer smallSmartPointer=m_fastLane->allocate(n,m_bytesPerElement,&group,
		m_cellContents,m_cellOccupancies );

	/** build the SmartPointer with the
 *	SmallSmartPointer, DefragmentationLane id, and DefragmentationGroup id 
* SmartPointer are unique contrary to SmallSmartPointer which are not
*/
	int globalGroup=m_fastLane->getNumber()*GROUPS_PER_LANE+group;
	SmartPointer smartPointer=globalGroup*ELEMENTS_PER_GROUP+smallSmartPointer;

	return smartPointer;
}

/**
 * deallocate liberates the space.
 * the bitmap is updated and m_allocatedSizes is set to 0 for a.
 * Finally, a call to defragment is performed.
 */
void ChunkAllocatorWithDefragmentation::deallocate(SmartPointer a){
	/** NULL is easy to free 
 * 	Not sure if the code should vomit an error instead.. */
	if(a==SmartPointer_NULL)
		return;

	/** get the DefragmentationGroup */
	int group=a/ELEMENTS_PER_GROUP;
	int correctLaneId=group/GROUPS_PER_LANE;
	int groupInLane=group%GROUPS_PER_LANE;

	/** forward the SmallSmartPointer to the DefragmentationGroup */
	// this may trigger some defragmentation
	SmallSmartPointer smallSmartPointer=a%ELEMENTS_PER_GROUP;
	m_defragmentationLanes[correctLaneId]->getGroup(groupInLane)->deallocate(smallSmartPointer,m_bytesPerElement,m_cellContents,m_cellOccupancies);
}

/** this one is easy,
 * Just forward the SmallSmartPointer to the DefragmentationGroup...
 */
void*ChunkAllocatorWithDefragmentation::getPointer(SmartPointer a){
	if(a==SmartPointer_NULL)
		return NULL;

	// to get the virtual address from a SmartPointer,
	// we need to find in which DefragmentationGroup it is
	// and then we just ask this DefragmentationGroup
	// for what we want
	int group=a/ELEMENTS_PER_GROUP;
	int correctLaneId=group/GROUPS_PER_LANE;
	int groupInLane=group%GROUPS_PER_LANE;

	/** the DefragmentationGroup knows how to do that */
	SmallSmartPointer smallSmartPointer=a%ELEMENTS_PER_GROUP;
	return m_defragmentationLanes[correctLaneId]->getGroup(groupInLane)->getPointer(smallSmartPointer,m_bytesPerElement);
}

// empty constructor
ChunkAllocatorWithDefragmentation::ChunkAllocatorWithDefragmentation(){}

// empty desctructor
ChunkAllocatorWithDefragmentation::~ChunkAllocatorWithDefragmentation(){}

