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


#include <stdarg.h>
#include <stdio.h>      /* vsnprintf */
#include <ctype.h>
#include <string.h>

#include "Enclave.h"
#include "Enclave_t.h"  /* print_string */

#include "../include/common.h"

char enclaveData[2048] = { 0 };

Accumulator* listAccumulator = NULL;

int exceeded = 0;

const char* getfield(char* line, int num)
{
    const char* tok;
    for (tok = strtok(line, ","); tok && *tok; tok = strtok(NULL, ",\n"))
    {
        if (!--num)
            return tok;
    }
    return NULL;
}

struct TestData
{
	sgx_spinlock_t 	spinlock 	= SGX_SPINLOCK_INITIALIZER;
	char			data[2048]	= { 0 };
};
TestData testData[MAX_SIZE];

void SingleMap(void* data)
{
	static int indexTestData = 0;

	char result[2048] = { 0 };

	char enclaveData1[2048] = { 0 };
	char enclaveData2[2048] = { 0 };
	strlcpy(enclaveData1, (char*)data, strlen((char*)data) + 1);
	strlcpy(enclaveData2, (char*)data, strlen((char*)data) + 1);

	const char* name = getfield(enclaveData1, 9);
	const char* delay = getfield(enclaveData2, 15);

	//char result[2048] = { 0 };
	strlcpy(result, name, sizeof(name));
	strncat(result, ",", 1);
	strncat(result, delay, strlen(delay));

	while (1)
	{
		sgx_spin_lock( &(testData[indexTestData].spinlock) );
		if (testData[indexTestData].data[0] == '\0')
		{
			strlcpy(testData[indexTestData].data, result, sizeof(result));
			sgx_spin_unlock( &(testData[indexTestData].spinlock) );
			indexTestData = (indexTestData + 1) % MAX_SIZE;
			break;
		}
		sgx_spin_unlock( &(testData[indexTestData].spinlock) );
		indexTestData = (indexTestData + 1) % MAX_SIZE;
	}

}

void SingleFilter(void* data)
{
	static int indexTestData = 0;

	char mapData[2048] = { 0 };

	strlcpy(mapData, (char*)data, strlen((char*)data) + 1);

	const char* delay = getfield(mapData, 2);
	int arrDelay = atoi(delay);

	if (arrDelay > 0)
	{
		while (1)
		{
			sgx_spin_lock( &(testData[indexTestData].spinlock) );
			if (testData[indexTestData].data[0] == '\0')
			{
				strlcpy(testData[indexTestData].data, (char*)data, sizeof((char*)data));
				sgx_spin_unlock( &(testData[indexTestData].spinlock) );
				indexTestData = (indexTestData + 1) % MAX_SIZE;
				break;
			}
			sgx_spin_unlock( &(testData[indexTestData].spinlock) );
			indexTestData = (indexTestData + 1) % MAX_SIZE;
		}	
	}
}


void SingleReduce(void* data)
{
	char filterData1[2048] = { 0 };
	char filterData2[2048] = { 0 };
	strlcpy(filterData1, (char*)data, strlen((char*)data) + 1);
	strlcpy(filterData2, (char*)data, strlen((char*)data) + 1);

	const char* uniqueCarrier = getfield(filterData1, 1);
	const char* delay = getfield(filterData2, 2);
	float arrDelay = atoi(delay);

	Accumulator* findie = listAccumulator;
	bool find = false;
	
	if (findie != NULL)
	{
		while(1)
		{
			if (!strcmp(uniqueCarrier, findie->uniqueCarrier)) 
			{
				find = true;
				break;
			}
			if (findie->next != NULL)
			{
				findie = findie->next;
			}
			else break;
		}

		if (find)
		{
			findie->count++;
			findie->delay += arrDelay;
		}
		else
		{
			Accumulator* newCarrier = new Accumulator;
			strlcpy(newCarrier->uniqueCarrier, uniqueCarrier, sizeof(uniqueCarrier));
			newCarrier->count++;
			newCarrier->delay += arrDelay;
			findie->next = newCarrier;
		}
	}
	else
	{
		Accumulator* newCarrier = new Accumulator;
		strlcpy(newCarrier->uniqueCarrier, uniqueCarrier, sizeof(uniqueCarrier));
		newCarrier->count++;
		newCarrier->delay += arrDelay;
		listAccumulator = newCarrier;
	}

	// finish Reduce process, wait for untrusted section to retrieve data
	// write another Ecall to transform Accumulator structure information to char array

}

struct DoneProcessing
{
	sgx_spinlock_t	spinlock;
	bool 			isDone = false;
};
DoneProcessing doneProcessing;

void EcallStartResponder( HotCall* hotEcall, CheckIsDone* checkIsDone )
{
	void (*callbacks[3])(void*);
	callbacks[0] = SingleMap;
	callbacks[1] = SingleFilter;
	callbacks[2] = SingleReduce;

    HotCallTable callTable;
    callTable.numEntries = 3;
    callTable.callbacks  = callbacks;

    HotCall_waitForCall( hotEcall, &callTable, checkIsDone );

	sgx_spin_lock( &(doneProcessing.spinlock) );
	doneProcessing.isDone = true;
	sgx_spin_unlock( &(doneProcessing.spinlock) );

	printf("Enclave out\n");
}

void ChangeIntToCharArray(int num, char* dst)
{
	char temp[2048] = { 0 };
	int index = 0;
	while(num)
	{
		int spare = num % 10;
		
		temp[index] = spare + 48;
		index += 1;

		num = num / 10;
	}

	for (int i = 0; i < index; i++)
	{
		//printf("%s", temp[index - 1]);
		dst[i] = temp[index - 1 - i];
	}

	dst[index] = '\0';
}

void EcallCopyDataNoMultithread(HotCall* hotOcall)
{
	const uint16_t requestedCallID = 0; // code them callID ben untrusted

	//SaveData* traversing = saveData;
	Accumulator* traversing = listAccumulator;

	char 		tempArray[2048];
	char 		enclaveCharArray[MAX_SIZE][2048];
	char*		pointerToEnclaveCharArray[MAX_SIZE];

	char 		count[2048] = { 0 };
	char 		delay[2048] = { 0 };

	for (int i = 0; i < MAX_SIZE; i++)
	{
		pointerToEnclaveCharArray[i] = enclaveCharArray[i];
	}

	int index = 0;

	while (traversing != NULL)
	{
		//strlcpy(tempArray, traversing->data, strlen(traversing->data) + 1);

		strlcpy(tempArray, traversing->uniqueCarrier, strlen(traversing->uniqueCarrier) + 1);
		strncat(tempArray, ": ", 2);

		ChangeIntToCharArray(traversing->count, count);
		ChangeIntToCharArray(traversing->delay, delay);

		strncat(tempArray, count, sizeof(count));
		strncat(tempArray, " carriers, total delay time ", 28);
		strncat(tempArray, delay, sizeof(delay));		

		int noti = HotCall_requestCall_v2(hotOcall, requestedCallID, (void **)pointerToEnclaveCharArray, (void *)tempArray, index);
		if (noti == -1)
		{
			exceeded += 1;
		}		

		traversing = traversing->next;

		index = (index + 1) % MAX_SIZE;
	}

	Accumulator* deleting = listAccumulator;
	traversing = listAccumulator;

	while (traversing != NULL)
	{
		deleting = traversing;
		traversing = traversing->next;
		delete deleting;
	}

	listAccumulator = traversing = deleting = NULL;

	printf("[+] Enclave: %d attempts exceeded retry limits\n", exceeded);
}

void EcallCopyDataWithMultithread(HotCall* hotOcall)
{
	static int indexWriteTestData = 0;

	const uint16_t requestedCallID = 0;

	char 		tempArray[2048];
	char 		enclaveCharArray[MAX_SIZE][2048];
	char*		pointerToEnclaveCharArray[MAX_SIZE];

	int countDown = MAX_SIZE;

	for (int i = 0; i < MAX_SIZE; i++)
	{
		pointerToEnclaveCharArray[i] = enclaveCharArray[i];
	}

	while(1)
	{
		sgx_spin_lock( &(doneProcessing.spinlock) );
		if (doneProcessing.isDone == true)
		{
			sgx_spin_unlock( &(doneProcessing.spinlock) );
			
			if (countDown > 0)
			{
				countDown -= 1;
			}
			else break;
		}
		sgx_spin_unlock( &(doneProcessing.spinlock) );

		sgx_spin_lock( &(testData[indexWriteTestData].spinlock));
		if (testData[indexWriteTestData].data[0] != '\0')
		{
			strlcpy(tempArray, testData[indexWriteTestData].data, sizeof(testData[indexWriteTestData].data));
			testData[indexWriteTestData].data[0] = '\0';
			sgx_spin_unlock( &(testData[indexWriteTestData].spinlock));
			indexWriteTestData = (indexWriteTestData + 1) % MAX_SIZE;
			
			int noti = HotCall_requestCall_v2(hotOcall, requestedCallID, (void **)pointerToEnclaveCharArray, (void *)tempArray, indexWriteTestData);
			if (noti == -1)
			{
				exceeded += 1;
			}

			continue;
		}
		sgx_spin_unlock( &(testData[indexWriteTestData].spinlock));
		indexWriteTestData = (indexWriteTestData + 1) % MAX_SIZE;
	}

	printf("[+] Enclave: %d attempts exceeded retry limits\n", exceeded);
}

/* 
 * printf: 
 *   Invokes OCALL to display the enclave buffer to the terminal.
 */
void printf(const char *fmt, ...)
{
    char buf[BUFSIZ] = {'\0'};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    ocall_print_string(buf);
}
