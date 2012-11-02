/*
 	Ray
    Copyright (C) 2010, 2011  Sébastien Boisvert

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

//#define CONFIG_ROUTING_KAUTZ_COMPUTE_ROUTES

#include <routing/GraphImplementationKautz.h>
#include <iostream>
using namespace std;

int GraphImplementationKautz::getPower(int base,int exponent){
	int a=1;
	while(exponent--)
		a*=base;
	return a;
}

/**
 *  convert a number to a de Bruijn vertex
 */
void GraphImplementationKautz::convertToBase(int i,Tuple*tuple){
	for(int power=0;power<m_diameter;power++){
		int value=(i%getPower(m_base,power+1))/getPower(m_base,power);
		tuple->m_digits[power]=value;
	}
}

bool GraphImplementationKautz::isAPowerOf(int n,int base){
	int remaining=n;
	
	while(remaining>1){
		if(remaining%base != 0)
			return false;
		remaining/=base;
	}
	return true;
}

/**
 * a Kautz vertex has no identical consecutive symbols
 */
bool GraphImplementationKautz::isAKautzVertex(Tuple*vertex){
	for(int i=0;i<m_diameter-1;i++){
		if(vertex->m_digits[i]==vertex->m_digits[i+1])
			return false;
	}
	return true;
}

void GraphImplementationKautz::configureGraph(int n){
	// given a degree k and a diameter d,
	// the number of vertices is (k+1)*k^(d-1)
	//
	// let's see if we can find a match.
	
	bool found=false;
	for(int degree=2;degree<=1024;degree++){
		for(int diameter=2;diameter<10;diameter++){
			int vertices=(degree+1)*getPower(degree,diameter-1);
			if(vertices==n){
				found=true;
				m_degree=degree;
				m_diameter=diameter;
				m_base=m_degree+1;
				m_size=n;
		
				break;
			}
		}
	}

	if(!found){
		cout<<"Error: cannot create a Kautz graph with "<<n<<" vertices"<<endl;
		m_base=-1;
	}
}

void GraphImplementationKautz::makeConnections(int n){
	
	configureGraph(n);

	int iterator=0;

	cout<<"[GraphImplementationKautz::makeConnections] degree= "<<m_degree<<" diameter= "<<m_diameter;
	cout<<" base= "<<m_base<<" vertices= "<<n<<endl;

	while((int)m_graphToKautz.size()<n){
		Tuple kautzVertex;
		convertToBase(iterator,&kautzVertex);

		if(isAKautzVertex(&kautzVertex)){
			// direct mapping
			m_graphToKautz.push_back(kautzVertex);
			
			// reverse mapping
			m_kautzToGraph[iterator]=m_graphToKautz.size()-1;
		}

		iterator++;
	}

	// create empty lists
	for(int i=0;i<m_size;i++){
		set<Rank> b;
		m_outcomingConnections.push_back(b);
		m_incomingConnections.push_back(b);
	}

	// make all connections.
	for(Rank i=0;i<m_size;i++){
		for(Rank j=0;j<m_size;j++){
			if(computeConnection(i,j)){
				m_outcomingConnections[i].insert(j);
				m_incomingConnections[j].insert(i);
			}
		}
	}

	cout<<"Done computing connections"<<endl;
}

/* base m_base to base 10 */
int GraphImplementationKautz::convertToBase10(Tuple*vertex){
	int a=0;
	for(int i=0;i<m_diameter;i++){
		a+=vertex->m_digits[i]*getPower(m_base,i);
	}
	return a;
}

void GraphImplementationKautz::printVertex(Tuple*a){
	for(int i=0;i<m_diameter;i++){
		if(i!=0)
			cout<<",";
		cout<<(int)a->m_digits[i];
	}
}

/** with de Bruijn routing, no route are pre-computed at all */
void GraphImplementationKautz::computeRoute(Rank source,Rank destination,vector<Rank>*route){
	/* do nothing because this is not utilised */

	Rank currentVertex=source;
	route->push_back(currentVertex);

	while(currentVertex!=destination){
		currentVertex=computeNextRankInRoute(source,destination,currentVertex);
		route->push_back(currentVertex);
	}
}

Rank GraphImplementationKautz::getNextRankInRoute(Rank source,Rank destination,Rank rank){
	#ifdef CONFIG_ROUTING_DE_KAUTZ_COMPUTE_ROUTES

	#ifdef ASSERT
	assert(m_routes[source][destination].count(rank)==1);
	#endif

	return m_routes[source][destination][rank];

	#else /* compute it right away */

	return computeNextRankInRoute(source,destination,rank);

	#endif
}

/** with de Bruijn routing, no route are pre-computed at all */
void GraphImplementationKautz::makeRoutes(){
	/* we don't compute any routes */
	
	#ifdef CONFIG_ROUTING_DE_KAUTZ_COMPUTE_ROUTES
	computeRoutes();
	#endif

	/* compute relay points */
	computeRelayEvents();
}

/** to get the next rank,
 * we need to shift the current one time on the left
 * then, we find the maximum overlap
 * between the current and the destination
 *
 * This value is the index of the digit in destination
 * that we want to append to the next rank in the route
 */
Rank GraphImplementationKautz::computeNextRankInRoute(Rank source,Rank destination,Rank current){
	Tuple*currentVertex=&(m_graphToKautz[current]);
	Tuple*destinationVertex=&(m_graphToKautz[destination]);

	// do a left shift
	Tuple next;
	for(int i=1;i<m_diameter;i++){
		next.m_digits[i-1]=currentVertex->m_digits[i];
	}
	
	int overlapSize=getMaximumOverlap(currentVertex,destinationVertex);

	// append the digit
	next.m_digits[m_diameter-1]=destinationVertex->m_digits[overlapSize];
	
	int inBase10=convertToBase10(&next);

	// get the corresponding MPI rank for the Kautz vertex
	int nextRank=m_kautzToGraph[inBase10];

	return nextRank;
}

/**
 * here we find the maximum overlap between
 * 2 de Bruijn vertices
 *
 * we don't look for a perfect match
 */
int GraphImplementationKautz::getMaximumOverlap(Tuple*a,Tuple*b){

	// we don't verify if they are exact matches
	// because if it would be the case, nothing would
	// need to be routed anywhere
	int numberOfMatches=m_diameter-1;

	while(1){
		// check for numberOfMatches
		int positionInA=m_diameter-numberOfMatches;
		int positionInB=0;

		bool match=true;

		while(positionInA<m_diameter){
			if(a->m_digits[positionInA] != b->m_digits[positionInB]){
				match=false;
				break;
			}
			positionInA++;
			positionInB++;
		}

		if(match)
			return numberOfMatches;

		numberOfMatches--;
	}

	return 0; /* will never be reached */
}

bool GraphImplementationKautz::isConnected(Rank source,Rank destination){
	if(source==destination)
		return true;

	return m_outcomingConnections[source].count(destination)==1;
}

/** 
 * just verify the overlap property
 */
bool GraphImplementationKautz::computeConnection(Rank source,Rank destination){

	// fetch the tuples from the cache table
	Tuple*sourceVertex=&(m_graphToKautz[source]);
	Tuple*destinationVertex=&(m_graphToKautz[destination]);

	// compute the maximum overlap
	int overlap=getMaximumOverlap(sourceVertex,destinationVertex);

	bool connected=overlap==m_diameter-1;

	return connected;
}

bool GraphImplementationKautz::isValid(int n){
	configureGraph(n);

	return m_base!=-1;
}

