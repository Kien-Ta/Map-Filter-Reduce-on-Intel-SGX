// ----------------------------------------
// HotCalls
// Copyright 2017 The Regents of the University of Michigan
// Ofir Weisse, Valeria Bertacco and Todd Austin

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ---------------------------------------------

//Author: Ofir Weisse, www.OfirWeisse.com, email: oweisse (at) umich (dot) edu
//Based on ISCA 2017 "HotCalls" paper. 
//Link to the paper can be found at http://www.ofirweisse.com/previous_work.html
//If you make nay use of this code for academic purpose, please cite the paper. 



#ifndef __FAST_SGX_CALLS_H
#define __FAST_SGX_CALLS_H


// #include <stdlib.h>
#include <sgx_spinlock.h>
#include <stdbool.h>
// #include "utils.h"

#include <unistd.h>
//#include <string.h>
//#include <cstring>

#pragma GCC diagnostic ignored "-Wunused-function"

#define MAX_SIZE 500

typedef unsigned long int pthread_t;

typedef struct {
    //pthread_t       responderThread;
    sgx_spinlock_t  spinlock;
    void*           data;
    uint16_t        callID;
    bool            keepPolling;
    //bool            runFunction;
    //bool            isDone;
    bool            busy;
} HotCall;

typedef struct 
{
    uint16_t numEntries;
    void (**callbacks)(void*);
} HotCallTable;

typedef struct {
    sgx_spinlock_t  spinlock;
    bool            keepPolling;
} CheckIsDone;

typedef struct {
    HotCall*        hotCall;
    CheckIsDone*    checkIsDone;
} PassToThread;

//#define HOTCALL_INITIALIZER  {0, SGX_SPINLOCK_INITIALIZER, NULL, 0, true, false, false, false }
//#define HOTCALL_INITIALIZER  {SGX_SPINLOCK_INITIALIZER, NULL, 0, true, false, false, false }
#define HOTCALL_INITIALIZER  {SGX_SPINLOCK_INITIALIZER, NULL, 0, true, false}

static void HotCall_init( HotCall* hotCall )
{
    //hotCall->responderThread    = 0;
    hotCall->spinlock           = SGX_SPINLOCK_INITIALIZER;
    hotCall->data               = NULL; 
    hotCall->callID             = 0;
    hotCall->keepPolling        = true;
    //hotCall->runFunction        = false;
    //hotCall->isDone             = false;
    hotCall->busy               = false;
}

static void CheckIsDone_init( CheckIsDone* checkIsDone)
{
    checkIsDone->spinlock       = SGX_SPINLOCK_INITIALIZER;
    checkIsDone->keepPolling    = true;
}

static inline void _mm_pause(void) __attribute__((always_inline));
static inline void _mm_pause(void)
{
    __asm __volatile(
        "pause"
    );
}

static inline int HotCall_requestCall_v2( HotCall* hotCall, uint16_t callID, void **dataArray, void *data, int index)
{
    int i = 0; 
    const uint32_t MAX_RETRIES = 100;
    uint32_t numRetries = 0;

    while ( true )
    {
        sgx_spin_lock( &(hotCall[index].spinlock) );
        if ( hotCall[index].busy == false && hotCall[index].data == NULL)
        {
            hotCall[index].busy         = true;
            hotCall[index].callID       = callID;
            
            strncpy((char *)dataArray[index], (char *)data, strlen((char *)data) + 1);
            hotCall[index].data         = dataArray[index];
            sgx_spin_unlock( &(hotCall[index].spinlock) );
            break;
        }
    
        sgx_spin_unlock( &(hotCall[index].spinlock) );

        numRetries++;
        //if (numRetries > MAX_RETRIES)
        //   return -1;

        for ( i = 0; i < 10; i++)
            _mm_pause();
    }

    return numRetries;
}

static inline void HotCall_waitForCall( HotCall *hotCall, HotCallTable* callTable, CheckIsDone *checkIsDone )  __attribute__((always_inline));
static inline void HotCall_waitForCall( HotCall *hotCall, HotCallTable* callTable, CheckIsDone *checkIsDone ) 
{
    static int i;

    int index = 0;
    int empty = 0;
    int numRetries = 0;

    int countDown = MAX_SIZE;

    // volatile void *data;
    while( true )
    {
        // them mot shared struct cho requestCall va waitForCall
        // co mot bien de xac dinh xem requestCall da goi xong hay chua
        // neu goi xong waitForCall chi chay them dung max_size lan roi se dung

        sgx_spin_lock( &checkIsDone->spinlock );
        if( checkIsDone->keepPolling != true ) {
            sgx_spin_unlock( &checkIsDone->spinlock );

            if (countDown > 0)
            {
                countDown -= 1;
            }
            else
            {
                break;
            }
        }
        sgx_spin_unlock( &checkIsDone->spinlock);


        sgx_spin_lock( &(hotCall[index].spinlock) );
        if( hotCall[index].busy == true && hotCall[index].data != NULL)
        {
            volatile uint16_t callID = hotCall[index].callID;
            void *data = hotCall[index].data;
            sgx_spin_unlock( &(hotCall[index].spinlock) );
            if( callID < callTable->numEntries ) {
                // printf( "Calling callback %d\n", callID );
                callTable->callbacks[ callID ]( data );
                //callTable->callbacks[ callID+1 ]( (void*)index );
            }
            else {  
                // printf( "Invalid callID\n" );
                // exit(42);
            }
            // DoWork( hotCall->data );
            // data = (int*)hotCall->data;
            // printf( "Enclave: Data is at %p\n", data );
            // *data += 1;
            sgx_spin_lock( &(hotCall[index].spinlock) );
            //hotCall[index].isDone      = true;
            //hotCall[index].runFunction = false;
            hotCall[index].busy        = false;
            hotCall[index].data        = NULL;
        }
        
        sgx_spin_unlock( &(hotCall[index].spinlock) );
        for( i = 0; i<3; ++i)
            _mm_pause();
        
        // _mm_pause();
        //     _mm_pause();
        // _mm_pause();

        index = (index + 1) % MAX_SIZE;
    }

}
static inline void StopResponder( HotCall *hotCall );
static inline void StopResponder( HotCall *hotCall )
{
    for (int i = 0; i < MAX_SIZE; i++)
    {
        sgx_spin_lock( &(hotCall[i].spinlock) );
        hotCall[i].keepPolling = false;
        sgx_spin_unlock( &(hotCall[i].spinlock) );
    }
}

static inline void signalEnd ( CheckIsDone *checkIsDone)
{
    sgx_spin_lock( &checkIsDone->spinlock);
    checkIsDone->keepPolling = false;
    sgx_spin_unlock( &checkIsDone->spinlock);
}

#endif