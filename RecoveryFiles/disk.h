#pragma once
#include <Windows.h>
#include <fileapi.h>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <stack>
#include <memory>
using namespace std;

class Disk {
public:
	struct Entry {
		uint32_t sector;
		uint32_t offset;
		BYTE buffer[32];

		Entry(uint32_t sector, uint32_t offset, BYTE buffer[]);
	};

	HANDLE handle;
	uint32_t bytesPerSector;
	uint32_t sectorsPerCluster;
	uint32_t FAT32TableSector;
	uint32_t numberOfFATs;
	uint32_t sectorsPerFAT;
	uint32_t rootCluster;
	uint32_t sectorOfFirstCluster;
	uint32_t totalSectors;

	void readBootSector();
	void readSector(BYTE* dest, uint32_t sector, uint32_t numberOfBytes = 512, uint32_t offset = 0);
	void writeSector(BYTE* src, uint32_t sector, uint32_t numberOfBytes, uint32_t offset);
	void assignValueToFATTable(uint32_t cluster, BYTE buffer[]);
	void fillDeletedSignatureToCluster(uint32_t cluster);
	bool recoverEntry(stack<Entry>& potentialEntries);
	bool iterateCluster(bool isDirectory, uint32_t cluster, stack<Entry>& potentialEntries);
	uint32_t findNextCluster(uint32_t cluster);
	uint32_t findStartSector(uint32_t cluster);
	uint32_t findStartCluster(Entry entry);
	
public:
	Disk();
	Disk(char diskLetter);
	bool initilizeDiskHandle(char diskLetter);
	void recoverFiles();
};