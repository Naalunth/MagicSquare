#include <vector>
#include <list>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#include <atomic>

using namespace std;

typedef unsigned char uint8;
typedef unsigned long long uint64;

static uint8 allNumbers[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
//Optimierte Zugriffsreihenfolge
static uint8 numberOrder[16] = { 0, 1, 2, 3, 4, 8, 12, 5, 10, 15, 6, 7, 14, 11, 9, 13 };

bool* threadWaitStatus;
int numThreads = 4;


mutex PrintMutex;
int counter = 0;


struct numberarray
{
public:

	uint64 numbers;

	uint8 operator [] (uint8 index)
	{
		return ((numbers >> (index * 4)) % 16) + 1;
	}

	void set(uint8 index, uint64 number)
	{
		numbers = (numbers & ~((((uint64) 1 << 4) - 1) << (index * 4))) | ((number - 1) << (index * 4));
	}

	numberarray() : numbers(0) {}


	numberarray(const numberarray& other)
	{
		numbers = other.numbers;
	}

	numberarray& operator = (const numberarray& other)
	{
		numbers = other.numbers;
		return *this;
	}

	void swap(uint8 a, uint8 b)
	{
		uint8 c = operator[](a);
		set(a, operator[](b));
		set(b, c);
	}
};


struct Square
{
public:
	numberarray numbers;
	Square(){}
	Square(const Square& other) :numbers(other.numbers){}
	bool IsRowCorrect(int reihe)
	{
		return numbers[reihe * 4 + 0] + numbers[reihe * 4 + 1] + numbers[reihe * 4 + 2] + numbers[reihe * 4 + 3] == 34;
	}
	bool IsColumnCorrect(int spalte)
	{
		return numbers[0 * 4 + spalte] + numbers[1 * 4 + spalte] + numbers[2 * 4 + spalte] + numbers[3 * 4 + spalte] == 34;
	}
	bool AreDiagonalsCorrect()
	{
		return (numbers[0] + numbers[5] + numbers[10] + numbers[15] == 34) &&
			(numbers[3] + numbers[6] + numbers[9] + numbers[12] == 34);
	}
	bool IsDiagonal1Correct()
	{
		return numbers[0] + numbers[5] + numbers[10] + numbers[15] == 34;
	}
	bool IsDiagonal2Correct()
	{
		return numbers[3] + numbers[6] + numbers[9] + numbers[12] == 34;
	}
	bool AreSumsCorrect()
	{
		return IsRowCorrect(0) && IsRowCorrect(1) && IsRowCorrect(2) && IsRowCorrect(3) &&
			IsColumnCorrect(0) && IsColumnCorrect(1) && IsColumnCorrect(2) && IsColumnCorrect(3) &&
			AreDiagonalsCorrect();
	}
	bool SindSummenKorrektHack()
	{
		return IsRowCorrect(3) && IsColumnCorrect(1);
	}
	void Print()
	{
		PrintMutex.lock();
		wprintf(L" %i:\n", ++counter);
		for (int i = 0; i < 16; i++)
		{
			wprintf(L"%i\t", numbers[i]);
			if (!((i + 1) % 4)) wprintf(L"\n");
		}
		wprintf(L"\n\n");
		fflush(stdout);
		PrintMutex.unlock();
	}
};


struct SearchStruct
{
	numberarray rest;
	Square q;
	uint8 restlength;
	SearchStruct(numberarray& rest0, Square& q0, uint8 restlength0 = 16) : rest(rest0), q(q0), restlength(restlength0) {}
	SearchStruct(uint8 rest0[16], Square& q0, uint8 restlength0 = 16) : q(q0), restlength(restlength0)
	{
		for (int i = 0; i < 16; i++)
			rest.set(i, rest0[i]);
	}
	SearchStruct(){};
};


vector<SearchStruct*> SearchStack;
int stackPointer = 0;


condition_variable WorkQueueEmpty;
mutex WorkQueueMutex;

atomic_int lockedThreadCount;
atomic_bool allThreadsLocked;


//This method gets some items from the work buffer
vector<SearchStruct*> GetWork()
{
	unique_lock<mutex> lck(WorkQueueMutex);
	if (SearchStack.empty())
	{
		lockedThreadCount++;
		while (SearchStack.empty())
		{
			while (WorkQueueEmpty.wait_for(lck, chrono::seconds(1)) == cv_status::timeout)
			{
				if (allThreadsLocked)
				{
					return vector<SearchStruct*>();
				}
			}
		}
		lockedThreadCount--;
	}

	int n = (stackPointer + numThreads - 1) / numThreads;
	vector<SearchStruct*> s(n);
	for (int i = 0; i < n; i++)
	{
		s[i] = SearchStack[--stackPointer];
	}
	if ((stackPointer << 1) < SearchStack.size()) SearchStack.resize(stackPointer);
	return s;
}

//This method adds items to the work buffer
void AddWork(vector<SearchStruct*>& ss)
{
	unique_lock<mutex> lck(WorkQueueMutex);
	for (auto it = ss.begin(); it != ss.end(); it++)
	{
		if (stackPointer >= SearchStack.size())
		{
			SearchStack.push_back(*it);
			stackPointer++;
		}
		else
		{
			SearchStack[stackPointer++] = (*it);
		}
	}
	WorkQueueEmpty.notify_all();
}


//This method is called by every thread and does most of the work
void SubProcess()
{
	int size;
	int invsize;

	SearchStruct* current;
	vector<SearchStruct*> tmpWorkBuffer;
	vector<SearchStruct*> tmpResultBuffer;

	bool discontinue;

	for (;;)
	{
		tmpWorkBuffer = GetWork();
		if (allThreadsLocked)
		{
			return;
		}
		tmpResultBuffer.resize(0);
		for (int work = 0; work < tmpWorkBuffer.size(); work++)
		{
			current = tmpWorkBuffer[work];
			size = current->restlength;

			invsize = 16 - size;

			if (size <= 0)
			{
				if (current->q.SindSummenKorrektHack())
				{
					current->q.Print();
				}
				continue;
			}

			//This tries to eliminate as many possibilities as possible
			discontinue = false;

			if (invsize == 12) { if (!current->q.IsRowCorrect(1)) discontinue = true; }
			else {
				if (invsize < 12) {
					if (invsize == 7) { if (!current->q.IsColumnCorrect(0)) discontinue = true; }
					else {
						if (invsize < 7) {
							if (invsize == 4) { if (!current->q.IsRowCorrect(0)) discontinue = true; }
						}
						else {
							if (invsize == 10) { if (!current->q.IsDiagonal1Correct()) discontinue = true; }
						}
					}
				}
				else {
					if (invsize == 14){ if (!current->q.IsColumnCorrect(3)) discontinue = true; }
					else {
						if (invsize < 14) { /*insize == 13*/ if (!current->q.IsColumnCorrect(2)) discontinue = true; }
						else /*insize == 15*/ if (!current->q.IsDiagonal2Correct() || !current->q.IsRowCorrect(2)) discontinue = true;
					}
				}
			}
			/* a little bit simpler
			if (invsize == 4) { if (!q.IstReiheKorrekt(0)) continue; }
			else if (invsize == 7) { if (!q.IstSpalteKorrekt(0)) continue; }
			else if (invsize == 10) { if (!q.IstDiagonale1Korrekt()) continue; }
			else if (invsize == 12) { if (!q.IstReiheKorrekt(1)) continue; }
			else if (invsize == 13) { if (!q.IstSpalteKorrekt(2)) continue; }
			else if (invsize == 14) { if (!q.IstSpalteKorrekt(3)) continue; }
			else if (invsize == 15) { if (!q.IstDiagonale2Korrekt() || !q.IstReiheKorrekt(2)) continue; } */

			if (!discontinue) {
				unsigned int rBOffset = tmpResultBuffer.size();
				tmpResultBuffer.resize(rBOffset + size);
				current->restlength = size - 1;
				for (int i = size - 1; i >= 0; i--)
				{
					current->q.numbers.set(numberOrder[invsize], current->rest[i]);
					current->rest.swap(i, size - 1);

					tmpResultBuffer[rBOffset + i] = (SearchStruct*) malloc(sizeof SearchStruct);
					memcpy(tmpResultBuffer[rBOffset + i], current, sizeof SearchStruct);

					current->rest.swap(i, size - 1);
				}
			}

			free(current);


		} //for
		AddWork(tmpResultBuffer);

	} //for(;;)
} //SubProcess


void BuildSquares()
{
	lockedThreadCount = 0;
	allThreadsLocked = false;
	SearchStack.reserve(500000);
	SearchStack.push_back(new SearchStruct(allNumbers, Square()));
	stackPointer++;
	int n = thread::hardware_concurrency();
	if (n == 0)
		numThreads = 16;
	else
		numThreads = n;

	//Multithreading!
	threadWaitStatus = new bool[numThreads];
	thread** threads = new thread*[numThreads];
	for (int i = 0; i < numThreads; i++)
	{
		threadWaitStatus[i] = false;
		threads[i] = new thread(SubProcess);
	}

	for (;;)
	{
		if (lockedThreadCount >= numThreads)
		{
			allThreadsLocked = true;
			for (int i = 0; i < numThreads; i++)
			{
				threads[i]->join();
			}
			delete[] threads;
			return;
		}
	}

}


int main()
{
	char* stdobuf = new char[4096];
	setvbuf(stdout, stdobuf, _IOFBF, 4096);

	Square q;
	BuildSquares();
	wprintf(L"Done. Press Enter.\n");

	cin.clear();
	cin.ignore(INT_MAX, '\n');

	delete stdobuf;
	return 0;
}
