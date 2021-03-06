// lab6_3.cpp: определяет точку входа для консольного приложения.
//

#include "stdafx.h"
#include <stdlib.h>
#include <iostream>
#include <ctime>
#include <ratio>
#include <chrono>
#include <vector>
#include <algorithm>    // std::sort
#include <omp.h>
#include <mpi.h>


const int ARRAY_AMOUNT = (int)pow(2, 4);
const bool DEBUG = false;
int SIZE, RANK;

int* initData(int N)
{
	int * arr = new int[N];
	for (int i = 0; i < N; i++)
	{
		arr[i] = rand() % 1000;
	}
	return arr;
}

int get_pivot(int low, int high, int * arr)
{
	int amount_of_random_to_average = 3;
	int random = 0;
	for (int i = 0; i < amount_of_random_to_average; i++)
		random += arr[low + int(((double)(rand()) / RAND_MAX) * (high - low))];
	return random / amount_of_random_to_average;
}

int partitionWithPivot(int low, int high, int * arr, int pivot)
{
	int i = low;
	int j = high - 1;
	while (i <= j)
	{
		while (arr[i] < pivot) i++;
		while (arr[j] > pivot) j--;
		if (i <= j)
		{
			std::swap(arr[i], arr[j]);
			i++;
			j--;
		}
	}
	return i;
}

bool isPowerOfTwo(int n)
{
	return (n & (n - 1)) == 0;
}
int main(int argc, char **argv)
{
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &SIZE);
	MPI_Comm_rank(MPI_COMM_WORLD, &RANK);
	int * arr;
	MPI_Status status;
	// проверка на корректность количества потоков
	if (!isPowerOfTwo(SIZE) || SIZE < 2)
	{
		std::cout << "Sorry, you must use power of two number of processes" << std::endl;
		MPI_Finalize();
		return 8;
	}
	// генерация данных и их раскидка туда-сюда
	if (RANK == 0)
	{
		int * glarr = initData(ARRAY_AMOUNT);
		if (DEBUG) std::cout << ARRAY_AMOUNT << std::endl;
		int n = ARRAY_AMOUNT / SIZE;
		for (int i = 1; i < SIZE; i++)
		{
			MPI_Ssend(&n, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
			MPI_Ssend(&(glarr[i * n]), n, MPI_INT, i, 0, MPI_COMM_WORLD);
		}
		arr = new int[n];
		std::copy(glarr, glarr + n, arr);
	}
	else // прием данных
	{
		int n;
		MPI_Recv(&n, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);
		arr = new int[n];
		MPI_Recv(arr, n, MPI_INT, 0, 0, MPI_COMM_WORLD, &status);
	}
	int current_array_size = ARRAY_AMOUNT / SIZE;
	for (int group_size = SIZE; group_size > 1; group_size /= 2)
	{
		//базовый определяет элемент-центр
#pragma region BaseElemnt
		int pivot;
		if (RANK % group_size == 0)
		{
			//собирает
			pivot = get_pivot(0, current_array_size, arr);
			int temp;
			for (int i = 1; i < group_size; i++)
			{
				MPI_Recv(&temp, 1, MPI_INT, RANK + i, 0, MPI_COMM_WORLD, &status);
				pivot += temp;
			}
			// считает
			pivot /= group_size;
			// отправляет
			for (int i = 1; i < group_size; i++)
			{
				MPI_Ssend(&pivot, 1, MPI_INT, RANK + i, 0, MPI_COMM_WORLD);
			}
		}
		else
		{
			//отдает
			int temp = get_pivot(0, current_array_size, arr);
			MPI_Ssend(&temp, 1, MPI_INT, RANK - (RANK % group_size), 0, MPI_COMM_WORLD);
			//принимает
			MPI_Recv(&pivot, 1, MPI_INT, RANK - (RANK % group_size), 0, MPI_COMM_WORLD, &status);
		}
#pragma endregion

		//раскидывают 
#pragma region Partition
		int middle = partitionWithPivot(0, current_array_size, arr, pivot);
#pragma endregion

		int newLeftSize;
		int * lessArr = new int[0];
		int newRightSize;
		int * highArr = new int[0];
		// пересылают меньшее
		// (RANK / counter) % 2
#pragma region SendLower
		int leftSize = middle;
		int rightSize = current_array_size - middle;
		//определяем посылающих
		if (((RANK / (group_size / 2)) % 2) == 1)
		{
			if (DEBUG) 	std::cout << "Rank: " << RANK << ", group_size: " << group_size << ", SENDING LOW" << std::endl;
			MPI_Ssend(&leftSize, 1, MPI_INT, RANK - group_size / 2, 0, MPI_COMM_WORLD);
			if (leftSize > 0) MPI_Ssend(arr, leftSize, MPI_INT, RANK - group_size / 2, 0, MPI_COMM_WORLD);
		}
		else //принимающие
		{
			if (DEBUG) std::cout << "Rank: " << RANK << ", group_size: " << group_size << ", resv LOW" << std::endl;
			MPI_Recv(&newRightSize, 1, MPI_INT, RANK + group_size / 2, 0, MPI_COMM_WORLD, &status);
			if (newRightSize > 0)
			{
				delete[] highArr;
				highArr = new int[newRightSize];
				MPI_Recv(highArr, newRightSize, MPI_INT, RANK + group_size / 2, 0, MPI_COMM_WORLD, &status);
			}
		}

#pragma endregion

		if (DEBUG) std::cout << RANK << " out of SendLower" << std::endl;
		// пересылают большие и сборка
		// 1 - (RANK / counter) % 2
#pragma region SendHigher
//определяем посылающих
		if (((RANK / (group_size / 2)) % 2) == 0)
		{
			//отслыка
			if (DEBUG) 	std::cout << "Rank: " << RANK << ", group_size: " << group_size << ", SENDING high" << std::endl;
			MPI_Ssend(&rightSize, 1, MPI_INT, RANK + group_size / 2, 0, MPI_COMM_WORLD);
			if (rightSize > 0) MPI_Ssend(arr + leftSize, rightSize, MPI_INT, RANK + group_size / 2, 0, MPI_COMM_WORLD);
			//сборка
			if (DEBUG) std::cout << RANK << ": " << newRightSize << " " << leftSize << std::endl;
			int * temparr = new int[newRightSize + leftSize];
			if (DEBUG) std::cout << RANK << " i am pass the rubikon" << std::endl;
			std::copy(arr, arr + leftSize, temparr);			

			/*if (DEBUG)
			{
				std::cout << RANK << ": ";
				for (int t = 0; t < newLeftSize + rightSize; t++)
					std::cout << temparr[t] << " ";
				std::cout << std::endl;
			}*/
			if (newRightSize > 0)
			{
				std::copy(highArr, highArr + newRightSize, temparr + leftSize);

			}
			delete[] arr;
			arr = temparr;
			current_array_size = newRightSize + leftSize;
			if (DEBUG) std::cout << RANK << ": new arr size" << current_array_size << std::endl;
		}
		else //принимающие
		{
			//прием
			if (DEBUG) 	std::cout << "Rank: " << RANK << ", group_size: " << group_size << ", resv high" << std::endl;
			MPI_Recv(&newLeftSize, 1, MPI_INT, RANK - group_size / 2, 0, MPI_COMM_WORLD, &status);
			if (newLeftSize > 0)
			{
				delete[] lessArr;
				lessArr = new int[newLeftSize];
				MPI_Recv(lessArr, newLeftSize, MPI_INT, RANK - group_size / 2, 0, MPI_COMM_WORLD, &status);
			}
			if (DEBUG) std::cout << RANK << ": " << newLeftSize << " " << rightSize << std::endl;
			//сборка
			int * temparr = new int[newLeftSize + rightSize];
			//if (DEBUG) std::cout << RANK << " i am pass the rubikon" << std::endl;
			std::copy(arr + leftSize, arr + current_array_size, temparr);		
			/*if (DEBUG)
			{
				std::cout << RANK << ": ";
				for (int t = 0; t < newLeftSize + rightSize; t++)
					std::cout << temparr[t] << " ";
				std::cout << std::endl;
			}*/

			if (newLeftSize > 0)
			{
				std::copy(lessArr, lessArr + newLeftSize, temparr + rightSize);

			}
			delete[] arr;
			arr = temparr;
			current_array_size = newLeftSize + rightSize;
			if (DEBUG) std::cout << RANK << ": new arr size" << current_array_size << std::endl;
		}
#pragma endregion


		//встреча на тусу (крутота с своими комуникаторами внутри группы)
		if (DEBUG) std::cout << RANK << " before tusich" << std::endl;
		MPI_Barrier(MPI_COMM_WORLD);
		if (DEBUG)
		{
			if (RANK == 0)	std::cout << std::endl;
			MPI_Barrier(MPI_COMM_WORLD);
		}
		
	}
	
	//сортировка внутри процессов
#pragma region SortIn Processes
	std::sort(arr, arr + current_array_size);
#pragma endregion;

	
	//слияние
#pragma region Merging
	if (RANK == 0)
	{
		int * resarr = new int[ARRAY_AMOUNT];
		int current_beg = 0;
		std::copy(arr, arr + current_array_size, resarr);
		current_beg += current_array_size;
		for (int i = 1; i < SIZE; i++)
		{
			int newCurArrsize;
			int * tempArr;
			MPI_Recv(&newCurArrsize, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);
			if (newCurArrsize > 0)
			{
				tempArr = new int[newCurArrsize];
				MPI_Recv(tempArr, newCurArrsize, MPI_INT, i, 0, MPI_COMM_WORLD, &status);
				std::copy(tempArr, tempArr + newCurArrsize, resarr + current_beg);
				current_beg += newCurArrsize;
			}
		}

		for (int i = 0; i < ARRAY_AMOUNT; i++)
		{
			std::cout << resarr[i] << " ";
		}
		std::cout << std::endl;

	}
	else
	{
		MPI_Ssend(&current_array_size, 1, MPI_INT,0, 0, MPI_COMM_WORLD);
		if (current_array_size > 0)
		{
			MPI_Ssend(arr, current_array_size, MPI_INT, 0, 0, MPI_COMM_WORLD);
		}
	}
#pragma endregion

	MPI_Finalize();
	//std::system("pause");
    return 0;
}

