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

/*
 * Copyright (C) 2011-2016 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include <stdio.h>
#include <string.h>
#include <assert.h>

# include <unistd.h>
# include <pwd.h>
# define MAX_PATH FILENAME_MAX


#include "sgx_urts.h"
#include "App.h"
#include "Enclave_u.h"


#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include "../include/common.h"

#include <string.h>
#include <cstring>
#include <fstream>


sgx_enclave_id_t globalEnclaveID;

typedef sgx_status_t (*EcallFunction)(sgx_enclave_id_t, void* );

#define PERFORMANCE_MEASUREMENT_NUM_REPEATS 10000
#define MEASUREMENTS_ROOT_DIR               "measurments"

using namespace std;

inline __attribute__((always_inline))  uint64_t rdtscp(void)
{
        unsigned int low, high;

        asm volatile("rdtscp" : "=a" (low), "=d" (high));

        return low | ((uint64_t)high) << 32;
}

typedef struct _sgx_errlist_t {
    sgx_status_t err;
    const char *msg;
    const char *sug; /* Suggestion */
} sgx_errlist_t;

/* Error code returned by sgx_create_enclave */
static sgx_errlist_t sgx_errlist[] = {
    {
        SGX_ERROR_UNEXPECTED,
        "Unexpected error occurred.",
        NULL
    },
    {
        SGX_ERROR_INVALID_PARAMETER,
        "Invalid parameter.",
        NULL
    },
    {
        SGX_ERROR_OUT_OF_MEMORY,
        "Out of memory.",
        NULL
    },
    {
        SGX_ERROR_ENCLAVE_LOST,
        "Power transition occurred.",
        "Please refer to the sample \"PowerTransition\" for details."
    },
    {
        SGX_ERROR_INVALID_ENCLAVE,
        "Invalid enclave image.",
        NULL
    },
    {
        SGX_ERROR_INVALID_ENCLAVE_ID,
        "Invalid enclave identification.",
        NULL
    },
    {
        SGX_ERROR_INVALID_SIGNATURE,
        "Invalid enclave signature.",
        NULL
    },
    {
        SGX_ERROR_OUT_OF_EPC,
        "Out of EPC memory.",
        NULL
    },
    {
        SGX_ERROR_NO_DEVICE,
        "Invalid SGX device.",
        "Please make sure SGX module is enabled in the BIOS, and install SGX driver afterwards."
    },
    {
        SGX_ERROR_MEMORY_MAP_CONFLICT,
        "Memory map conflicted.",
        NULL
    },
    {
        SGX_ERROR_INVALID_METADATA,
        "Invalid enclave metadata.",
        NULL
    },
    {
        SGX_ERROR_DEVICE_BUSY,
        "SGX device was busy.",
        NULL
    },
    {
        SGX_ERROR_INVALID_VERSION,
        "Enclave version was invalid.",
        NULL
    },
    {
        SGX_ERROR_INVALID_ATTRIBUTE,
        "Enclave was not authorized.",
        NULL
    },
    {
        SGX_ERROR_ENCLAVE_FILE_ACCESS,
        "Can't open enclave file.",
        NULL
    },
};

struct Accumulator
{
	char uniqueCarrier[2048] = { 0 };
	int count = 0;
	float delay = 0;
} accumulator;

/* Check error conditions for loading enclave */
void print_error_message(sgx_status_t ret)
{
    size_t idx = 0;
    size_t ttl = sizeof sgx_errlist/sizeof sgx_errlist[0];

    for (idx = 0; idx < ttl; idx++) {
        if(ret == sgx_errlist[idx].err) {
            if(NULL != sgx_errlist[idx].sug)
                printf("Info: %s\n", sgx_errlist[idx].sug);
            printf("Error: %s\n", sgx_errlist[idx].msg);
            break;
        }
    }
    
    if (idx == ttl)
        printf("Error: Unexpected error occurred.\n");
}


/* OCall functions */
void ocall_print_string(const char *str)
{
    /* Proxy/Bridge will check the length and null-terminate 
     * the input string to prevent buffer overflow. 
     */
    printf("%s", str);
}

void* EnclaveResponderThread( void* passToThreadAsVoidP)
{
    //To be started in a new thread
    PassToThread *argPass = (PassToThread*)passToThreadAsVoidP;

    HotCall *hotEcall = argPass->hotCall;

    CheckIsDone *checkIsDone = argPass->checkIsDone;

    EcallStartResponder( globalEnclaveID, hotEcall, checkIsDone );

    return NULL;
}

void CopyData(void *data)
{
    strcpy(accumulator.uniqueCarrier, ((Accumulator*)data)->uniqueCarrier);
    accumulator.count = ((Accumulator*)data)->count;
    accumulator.delay = ((Accumulator*)data)->delay;

    printf("%d carrier %s total delay time: %f\n",accumulator.count, accumulator.uniqueCarrier, accumulator.delay);
}

int callID = 0;

void WriteDataToCSV(void* data)
{
    char filename[64] = { 0 };
    switch (callID)
    {
    case 1:
    case 4:
        strcpy(filename, "MapResult.csv");
        break;

    case 2:
    case 5:
        strcpy(filename, "FilterResult.csv");
        break;
    
    case 3:
        strcpy(filename, "ReduceResult.txt");
        break;

    default:
        strcpy(filename, "InsertGenericNameHere.ktt");
        break;
    }


    fstream fin;
    fin.open(filename, ios::in | ios::out | ios::app);

    fin.write((char *)data, strlen((char *)data));
    fin.write("\n", 1);

    fin.close();
}

void* OcallResponderThread( void* passToThreadAsVoidP)
{
    void (*callbacks[2])(void*);   
    callbacks[0] = CopyData;
    callbacks[1] = WriteDataToCSV;

    HotCallTable callTable;
    callTable.numEntries = 2;
    callTable.callbacks  = callbacks;

    PassToThread* argPass = (PassToThread*)passToThreadAsVoidP;

    HotCall* hotOcall = argPass->hotCall;

    CheckIsDone* checkIsDone = argPass->checkIsDone;

    HotCall_waitForCall( hotOcall, &callTable, checkIsDone );

    printf("Untrusted out\n");

    return NULL;
}

pthread_t testThread1 = 0, testThread2 = 0, testThread3 = 0;


class HotCallsTesterError {};

pthread_t thread1 = 0, thread2 = 0;

int exceeded = 0;



class HotCallsTester {
public:
    HotCallsTester() {
        m_enclaveID = 0;

        if( initialize_enclave() < 0){
            printf("Enter a character before exit ...\n");
            getchar();
            throw HotCallsTesterError(); 
        }

        //CreateMeasurementsDirectory();
    }

    ~HotCallsTester() {
        /* Destroy the enclave */
        sgx_destroy_enclave( m_enclaveID );
    }

    void Run( void ) 
    {
        callID = 3;

        uint64_t    startTime       = 0;
        uint64_t    endTime         = 0;

        char filename[64] =  { 0 };

        // fun code, might remove later
        switch (callID)
        {
        case 1:
        case 4:
            strcpy(filename, "2008.csv");
            break;

        case 2:
        case 5:
            strcpy(filename, "MapResult.csv");
            break;

        case 3:
            strcpy(filename, "FilterResult.csv");
            break;

        
        default:
            printf("WHAET? Commando no understando...\n");
            exit(-1);
        }
        
        startTime = rdtscp();

        CallEnclaveToDoYourBidding(filename, callID);
        
        //RetrieveResultFromEnclave();

        //StillCallEnclaveButWithMultithreading(filename, callID);

        endTime = rdtscp();

        printf("[+] Executed time: %ld\n", endTime - startTime);
        printf("[+] Untrusted: %d attempts exceeded retry limits\n", exceeded);
    }

    void CallEnclaveToDoYourBidding(const char* filename, const uint16_t requestedCallID)
    {
        string untrustedString;

        char        tempArray[2048];
        char        untrustedCharArray[MAX_SIZE][2048];
        char*       pointerToUntrustedCharArray[MAX_SIZE];

        for (int i = 0; i < MAX_SIZE; i++)
        {
            pointerToUntrustedCharArray[i] = untrustedCharArray[i];
        }

        HotCall     hotEcall[MAX_SIZE];
        for (int i = 0; i < MAX_SIZE; i++)
        {
            HotCall_init(&hotEcall[i]);
        }

        CheckIsDone checkIsDone;
        CheckIsDone_init(&checkIsDone);
        // tien hanh trien khai checkIsDone vao requestCall va waitForCall

        PassToThread passToThread;
        passToThread.hotCall = hotEcall;
        passToThread.checkIsDone = &checkIsDone;

        int index = 0;
        
        printf("%s\n", filename);

        fstream fin;
        fin.open(filename, ios::in);

        globalEnclaveID = m_enclaveID;

        //pthread_create(&hotEcall.responderThread, NULL, EnclaveResponderThread, (void*)&hotEcall);
        //pthread_create(&thread1, NULL, EnclaveResponderThread, (void*)hotEcall);

        int notiThread = pthread_create(&thread1, NULL, EnclaveResponderThread, (void*)&passToThread);
        if (notiThread != 0)
        {
            printf("SKYFALLLLLLLLLLLLLL!!!!!!!!!!!!!\n");
            exit(-1);
        }

        //int test = 0;

        while (getline(fin, untrustedString))
        {
            strcpy(tempArray, untrustedString.c_str());

            int noti = HotCall_requestCall_v2(hotEcall, requestedCallID, (void **)pointerToUntrustedCharArray, (void *)tempArray, index);

            if (noti == -1)
            {
                exceeded += 1;
            }

            //printf("[+] Untrusted: %d\n", test);
            //test = test + 1;

            index = (index + 1) % MAX_SIZE;
        }

        fin.close();

        signalEnd( &checkIsDone );

        pthread_join(thread1, NULL);

        this->RetrieveResultFromEnclave();

        //sleep(5);
        //StopResponder(hotEcall);
    }
   
    void RetrieveResultFromEnclave()
    {
        HotCall hotOcall[MAX_SIZE];
        for (int i = 0; i < MAX_SIZE; i++)
        {
            HotCall_init(&hotOcall[i]);
        }

        CheckIsDone checkIsDone;
        CheckIsDone_init(&checkIsDone);

        PassToThread passToThread;
        passToThread.hotCall = hotOcall;
        passToThread.checkIsDone = &checkIsDone;

        //pthread_create(&thread2, NULL, OcallResponderThread, (void*)hotOcall);
        int notiThread = pthread_create(&thread2, NULL, OcallResponderThread, (void*)&passToThread);
        if (notiThread != 0)
        {
            printf("SKYFALLLLLLLLLLLLLL!!!!!!!!!!!!!\n");
            exit(-1);   
        }

        if (callID != 3)
        {
            EcallCopyDataToUntrustedLand2(m_enclaveID, hotOcall);
        }
        else
        {
            EcallCopyDataToUntrustedLand3(m_enclaveID, hotOcall);
        }

        signalEnd( &checkIsDone );

        pthread_join(thread2, NULL);

        //sleep(10);
        //StopResponder(hotOcall);
    }

    void StillCallEnclaveButWithMultithreading(const char* filename, const uint16_t requestedCallID)
    {
        string      untrustedString;

        char        tempArray[2048] = { 0 };
        char        untrustedCharArray[MAX_SIZE][2048];
        char*       pointerToUntrustedCharArray[MAX_SIZE];

        HotCall     hotEcall[MAX_SIZE];

        for (int i = 0; i < MAX_SIZE; i++)
        {
            pointerToUntrustedCharArray[i] = untrustedCharArray[i];

            HotCall_init( &hotEcall[i]);
        }

        CheckIsDone checkIsDone;
        CheckIsDone_init(&checkIsDone);

        PassToThread passToThread;
        passToThread.hotCall        = hotEcall;
        passToThread.checkIsDone    = &checkIsDone;

        int index = 0;
        //int countingDown = 0;

        printf("%s\n", filename);

        fstream fin;
        fin.open(filename, ios::in);

        globalEnclaveID = m_enclaveID;

        int notiThread1 = pthread_create(&testThread1, NULL, EnclaveResponderThread, (void*)&passToThread);
        if (notiThread1 != 0)
        {
            printf("No Thread?\n");
            exit(-1);
        }

        int notiThread2 = pthread_create(&testThread2, NULL, StillRetrieveResultButSpawnedFromCall, NULL);
        if (notiThread2 != 0)
        {
            printf("No Thread?\n");
            exit(-1);
        }

        while (getline(fin, untrustedString))
        {
            strcpy(tempArray, untrustedString.c_str());

            int noti = HotCall_requestCall_v2(hotEcall, requestedCallID, (void **)pointerToUntrustedCharArray, (void *)tempArray, index);

            if (noti == -1)
            {
                exceeded += 1;
            }   

            //printf("[+] Untrusted: %d\n", countingDown);
            //countingDown += 1;
            index = (index + 1) % MAX_SIZE;
        }

        fin.close();

        signalEnd( &checkIsDone );

        pthread_join(testThread1, NULL);
        pthread_join(testThread2, NULL);
        pthread_join(testThread3, NULL);
    }


    static void* StillRetrieveResultButSpawnedFromCall(void* lorenIpsum)
    {
        HotCall hotOcall[MAX_SIZE];
        for (int i = 0; i < MAX_SIZE; i++)
        {
            HotCall_init(&hotOcall[i]);
        }

        CheckIsDone checkIsDone;
        CheckIsDone_init(&checkIsDone);

        PassToThread passToThread;
        passToThread.hotCall = hotOcall;
        passToThread.checkIsDone = &checkIsDone;

        //pthread_create(&thread2, NULL, OcallResponderThread, (void*)hotOcall);
        int notiThread = pthread_create(&testThread3, NULL, OcallResponderThread, (void*)&passToThread);
        if (notiThread != 0)
        {
            printf("SKYFALLLLLLLLLLLLLL!!!!!!!!!!!!!\n");
            exit(-1);   
        }

        if (callID != 3)
        {
            EcallCopyDataToUntrustedLand4(globalEnclaveID, hotOcall);
            
            // sum code goes here
        }
        else
        {
            //EcallCopyDataToUntrustedLand3(m_enclaveID, hotOcall);
        
            // TODO TODO TODO
        }

        signalEnd( &checkIsDone );

        pthread_join(testThread3, NULL);

        //sleep(10);

        return NULL;
    }



private:
    /* Global EID shared by multiple threads */
    sgx_enclave_id_t m_enclaveID;

    int              m_sgxDriver;
    string           m_measurementsDir;

    /* Initialize the enclave:
     *   Step 1: try to retrieve the launch token saved by last transaction
     *   Step 2: call sgx_create_enclave to initialize an enclave instance
     *   Step 3: save the launch token if it is updated
     */
    int initialize_enclave(void)
    {
        char token_path[MAX_PATH] = {'\0'};
        sgx_launch_token_t token = {0};
        sgx_status_t ret = SGX_ERROR_UNEXPECTED;
        int updated = 0;
        
        /* Step 1: try to retrieve the launch token saved by last transaction 
         *         if there is no token, then create a new one.
         */
        /* try to get the token saved in $HOME */
        const char *home_dir = getpwuid(getuid())->pw_dir;
        
        if (home_dir != NULL && 
            (strlen(home_dir)+strlen("/")+sizeof(TOKEN_FILENAME)+1) <= MAX_PATH) {
            /* compose the token path */
            strncpy(token_path, home_dir, strlen(home_dir));
            strncat(token_path, "/", strlen("/"));
            strncat(token_path, TOKEN_FILENAME, sizeof(TOKEN_FILENAME)+1);
        } else {
            /* if token path is too long or $HOME is NULL */
            strncpy(token_path, TOKEN_FILENAME, sizeof(TOKEN_FILENAME));
        }
        
        FILE *fp = fopen(token_path, "rb");
        if (fp == NULL && (fp = fopen(token_path, "wb")) == NULL) {
            printf("Warning: Failed to create/open the launch token file \"%s\".\n", token_path);
        }
        
        if (fp != NULL) {
            /* read the token from saved file */
            size_t read_num = fread(token, 1, sizeof(sgx_launch_token_t), fp);
            if (read_num != 0 && read_num != sizeof(sgx_launch_token_t)) {
                /* if token is invalid, clear the buffer */
                memset(&token, 0x0, sizeof(sgx_launch_token_t));
                printf("Warning: Invalid launch token read from \"%s\".\n", token_path);
            }
        }
        
        /* Step 2: call sgx_create_enclave to initialize an enclave instance */
        /* Debug Support: set 2nd parameter to 1 */
        ret = sgx_create_enclave(ENCLAVE_FILENAME, SGX_DEBUG_FLAG, &token, &updated, &m_enclaveID, NULL);
        if (ret != SGX_SUCCESS) {
            printf("sgx_create_enclave returned 0x%x\n", ret);
            print_error_message(ret);
            if (fp != NULL) fclose(fp);
            return -1;
        }
        
        /* Step 3: save the launch token if it is updated */
        if (updated == FALSE || fp == NULL) {
            /* if the token is not updated, or file handler is invalid, do not perform saving */
            if (fp != NULL) fclose(fp);
            return 0;
        }
        
        /* reopen the file with write capablity */
        fp = freopen(token_path, "wb", fp);
        if (fp == NULL) return 0;
        size_t write_num = fwrite(token, 1, sizeof(sgx_launch_token_t), fp);
        if (write_num != sizeof(sgx_launch_token_t))
            printf("Warning: Failed to save launch token to \"%s\".\n", token_path);
        fclose(fp);
        printf("line: %d\n", __LINE__ );
        return 0;
    }

};

/* Application entry */
int SGX_CDECL main(int argc, char *argv[])
{
    (void)(argc);
    (void)(argv);

    HotCallsTester hotCallsTester;
    hotCallsTester.Run();

    return 0;
}

