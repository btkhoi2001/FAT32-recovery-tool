#include "disk.h"

void Disk::readBootSector() {
	BYTE buffer[8];

	readSector(buffer, 0, 2, 0x0000000B);
	memcpy_s(&bytesPerSector, sizeof(bytesPerSector), buffer, 2);

	readSector(buffer, 0, 1, 0x0000000D);
	memcpy_s(&sectorsPerCluster, sizeof(sectorsPerCluster), buffer, 1);

	readSector(buffer, 0, 2, 0x0000000E);
	memcpy_s(&FAT32TableSector, sizeof(FAT32TableSector), buffer, 2);

	readSector(buffer, 0, 1, 0x00000010);
	memcpy_s(&numberOfFATs, sizeof(numberOfFATs), buffer, 1);

	readSector(buffer, 0, 4, 0x00000020);
	memcpy_s(&totalSectors, sizeof(totalSectors), buffer, 4);

	readSector(buffer, 0, 4, 0x00000024);
	memcpy_s(&sectorsPerFAT, sizeof(sectorsPerFAT), buffer, 4);

	readSector(buffer, 0, 4, 0x0000002C);
	memcpy_s(&rootCluster, sizeof(sectorsPerFAT), buffer, 4);

	readSector(buffer, 0, 8, 0x00000052);
	buffer[5] = '\0';

	if (strcmp((char*)buffer, "FAT32") != 0) {
		cerr << "This volume is not formatted as FAT32";
		exit(-1);
	}

	sectorOfFirstCluster = FAT32TableSector + numberOfFATs * sectorsPerFAT;
}

void Disk::readSector(BYTE* dest, uint32_t sector, uint32_t numberOfBytes, uint32_t offset) {
	BYTE* buffer = new BYTE[bytesPerSector];
	DWORD bytesRead;

	SetFilePointer(handle, bytesPerSector * sector, NULL, FILE_BEGIN);

	if (ReadFile(handle, (LPVOID)buffer, bytesPerSector, &bytesRead, NULL))
		memcpy(dest, &buffer[offset], numberOfBytes);

	delete[] buffer;
}

void Disk::writeSector(BYTE* src, uint32_t sector, uint32_t numberOfBytes, uint32_t offset) {
	BYTE* buffer = new BYTE[bytesPerSector];
	DWORD numberOfBytesWritten;

	readSector(buffer, sector, bytesPerSector);
	memcpy_s(buffer + offset, bytesPerSector, src, numberOfBytes);

	SetFilePointer(handle, bytesPerSector * sector, NULL, FILE_BEGIN);
	WriteFile(handle, buffer, bytesPerSector, &numberOfBytesWritten, NULL);

	delete[] buffer;
}

void Disk::assignValueToFATTable(uint32_t cluster, BYTE buffer[]) {
	uint32_t FATposition = FAT32TableSector + cluster / (bytesPerSector / 4);
	uint32_t offset = 4 * (cluster % (bytesPerSector / 4));

	writeSector(buffer, FATposition, 4, offset);
}

void Disk::fillDeletedSignatureToCluster(uint32_t cluster) {
	BYTE* buffer = new BYTE[bytesPerSector];
	uint32_t sector = findStartSector(cluster);

	for (int i = 0; i < sectorsPerCluster; i++) {
		readSector(buffer, sector + i, bytesPerSector, 0);

		for (int i = 0; i < bytesPerSector; i += 32)
			if (buffer[i] == 0x00)
				buffer[i] = 0xE5;

		writeSector(buffer, sector + i, bytesPerSector, 0);
	}

	delete[] buffer;
}

bool Disk::recoverEntry(stack<Entry>& potentialEntries) {
	stack<Entry> recoveryEntries;
	Entry entry = potentialEntries.top();
	uint32_t fileSize;
	uint32_t startCluster = findStartCluster(entry);
	int clusterChainRange;

	memcpy_s(&fileSize, 4, entry.buffer + 0x1C, 4);

	if (entry.buffer[0x0B] & 0b10000)
		clusterChainRange = 1;
	else
		clusterChainRange = max(1, (fileSize + bytesPerSector * sectorsPerCluster - 1) / (bytesPerSector * sectorsPerCluster));

	potentialEntries.pop();

	if (startCluster < 2)
		return false;

	// Cluster chain need to be unused in the FAT table
	for (int i = 0; i < clusterChainRange; i++)
		if (findNextCluster(startCluster + i) != 0x00000000)
			return false;

	// Recover entry
	if (entry.buffer[0x06] == 0x7E || entry.buffer[0x0B] & 0b10000) {
		// Long entry
		if (potentialEntries.empty())
			return false;

		// No need to assign a random letter since we can get the uncorrupted file name from the first long entry
		Entry firstLongEntry = potentialEntries.top();
		potentialEntries.pop();

		if (firstLongEntry.buffer[0x0B] != 0x0F || firstLongEntry.buffer[0x0C] != 0x00)
			return false;

		entry.buffer[0x00] = toupper(firstLongEntry.buffer[0x01]);

		uint8_t checkSum = 0;

		for (int i = 0; i < 11; i++)
			checkSum = (((checkSum & 1) << 7) | ((checkSum & 0xFE) >> 1)) + entry.buffer[i];

		if (firstLongEntry.buffer[0x0D] != checkSum)
			return false;

		firstLongEntry.buffer[0] = 0x01;

		recoveryEntries.push(entry);
		recoveryEntries.push(firstLongEntry);
		
		for (uint8_t i = 0x02; i <= 0x09; i++) {
			if (potentialEntries.empty())
				break;

			entry = potentialEntries.top();
			potentialEntries.pop();

			if (entry.buffer[0x0B] != 0x0F || entry.buffer[0x0C] != 0x00 || entry.buffer[0x0D] != checkSum)
				break;

			entry.buffer[0x00] = i;
			recoveryEntries.push(entry);
		}

		recoveryEntries.top().buffer[0] += 0x40;
	}
	else {
		// Short entry
		// Assign a random letter to the first character of the file name
		entry.buffer[0x00] = 'A' + rand() % 26;
		
		recoveryEntries.push(entry);
	}

	// Recover entry
	while (!recoveryEntries.empty()) {
		entry = recoveryEntries.top();
		recoveryEntries.pop();

		writeSector(entry.buffer, entry.sector, 32, entry.offset);
	}

	// Recover FAT table
	for (int i = 0; i < clusterChainRange; i++) {
		BYTE buffer[4];
		uint32_t cluster;

		if (i == clusterChainRange - 1)
			cluster = 0x0FFFFFFF;
		else
			cluster = startCluster + i + 1;

		memcpy_s(buffer, 4, &(cluster), 4);
		assignValueToFATTable(startCluster + i, buffer);
	}

	return true;
}

bool Disk::iterateCluster(bool isDirectory, uint32_t cluster, stack<Entry>& potentialEntries) {
	BYTE* buffer = new BYTE[bytesPerSector];
	uint32_t offset = 0;
	uint32_t nextCluster;
	uint32_t sector = findStartSector(cluster);
	bool isFileExist = false;
	bool isUnusedCluster = findNextCluster(cluster) == 0x00000000;

	if (isDirectory) {
		readSector(buffer, sector, 64, 0);

		if (buffer[0x00] != 0x2E || buffer[0x20] != 0x2E || buffer[0x21] != 0x2E)
			return false;

		offset = 64;
	}

	// Iterate through all entries and add a potential entry to stack
	for (int i = 0; i < sectorsPerCluster; i++) {
		readSector(buffer, sector + i, bytesPerSector, 0);

		for (int j = offset; j < bytesPerSector; j += 32) {
			Entry entry = Entry(sector + i, j, buffer + j);

			// Potential entry is an entry that might be can recovery
			// 1. The first offset has a deleted signature (0xE5)
			// 2. If the cluster is a directory or unused cluster, some entries mightn't have a deleted signature
			// We will add all of them to the stack
			if (entry.buffer[0x00] == 0xE5 || isDirectory || isUnusedCluster) {
				potentialEntries.push(entry);

				// If the offset 0x0B doesn't have value 0x0F (long entry), it is a short entry
				// Then we start to recover from this entry
				if (entry.buffer[0x0B] != 0x0F) {
					if (recoverEntry(potentialEntries)) {
						isFileExist = true;

						// If this entry points to a directory cluster, iterate that cluster
						if (entry.buffer[0x0B] & 0b10000)
							iterateCluster(true, findStartCluster(entry), potentialEntries);
					}

					// Erase stack
					potentialEntries = stack<Entry>();
				}
			}
			else if (!isUnusedCluster && entry.buffer[0x0B] & 0b10000)
				// If this entry is in the root cluster chain and points to a directory cluster, iterate that cluster
				iterateCluster(true, findStartCluster(entry), potentialEntries);
		}

		offset = 0;
	}

	return isFileExist;
}

uint32_t Disk::findNextCluster(uint32_t cluster) {
	BYTE buffer[4];
	uint32_t FATposition = FAT32TableSector + cluster / (bytesPerSector / 4);
	uint32_t offset = 4 * (cluster % (bytesPerSector / 4));

	if (FATposition >= FAT32TableSector + sectorsPerFAT)
		return -1;

	readSector(buffer, FATposition, 4, offset);

	return *(uint32_t*)(buffer);
}

uint32_t Disk::findStartSector(uint32_t cluster) {
	return sectorOfFirstCluster + (cluster - rootCluster) * sectorsPerCluster;
}

uint32_t Disk::findStartCluster(Entry entry) {
	uint32_t startCluster = 0;
	BYTE buffer[4];

	memcpy_s(buffer + 2, 2, entry.buffer + 0x14, 2); // High word
	memcpy_s(buffer, 2, entry.buffer + 0x1A, 2); // Low word
	memcpy_s(&startCluster, 4, buffer, 4);

	return startCluster;
}

Disk::Disk() {
	handle = nullptr;
	bytesPerSector = 512;
	sectorsPerCluster = 2;
	FAT32TableSector = 0;
	numberOfFATs = 2;
	sectorsPerFAT = 0;
	rootCluster = 2;
	sectorOfFirstCluster = 0;
	totalSectors = 1024;
}

Disk::Disk(char diskLetter) : Disk() {
	initilizeDiskHandle(diskLetter);
}

bool Disk::initilizeDiskHandle(char diskLetter) {
	char fileName[7] = "\\\\.\\?:";

	fileName[4] = diskLetter;
	DWORD dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
	DWORD dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
	LPSECURITY_ATTRIBUTES lpSecurityAttributes = NULL;
	DWORD dwCreationDisposition = OPEN_EXISTING;
	DWORD dwFlagsAndAttributes = 0;
	HANDLE hTemplateFile = NULL;

	handle = CreateFileA(fileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
	
	readBootSector();
	DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, NULL, NULL);

	return handle != INVALID_HANDLE_VALUE;
}

void Disk::recoverFiles() {
	BYTE buffer[4];
	stack<Entry> potentialEntries;
	uint32_t lastCluster = rootCluster;
	uint32_t currentCluster = rootCluster;
	
	// Iterate through all clusters that are in the root directory cluster chain or are unused
	while (findStartSector(currentCluster) < totalSectors) {
		bool isFileExist = iterateCluster(false, currentCluster, potentialEntries);
		uint32_t nextCluster = findNextCluster(currentCluster);

		// 1. If the next cluster has a value 0x0FFFFFFF, the root cluster chain ends here.
		// 2. If the next cluster has a value 0x00000000, It's an unused cluster. We call it is a potential cluster since we don't know
		// it has deleted entries or not. If we iterate through all entries of this cluster, the variable isFileExist will be true.
		// Then we will add this cluster to the root cluster chain.
		// 3. If the current cluster is in the root cluster chain, then fill the value 0xE5 to the first offset of all unused entries

		if (nextCluster == 0x00000000 || nextCluster == 0x0FFFFFFF) {

			if (nextCluster == 0x00000000) {
				if (isFileExist) {
					fillDeletedSignatureToCluster(currentCluster);

					// Last cluster points to the current cluster
					memcpy_s(buffer, 4, &(currentCluster), 4);
					assignValueToFATTable(lastCluster, buffer);


					// Assign end signature to the current cluster
					nextCluster = 0x0FFFFFFF;

					memcpy_s(buffer, 4, &nextCluster, 4);
					assignValueToFATTable(currentCluster, buffer);

					lastCluster = currentCluster;
				}
			}
			else
				fillDeletedSignatureToCluster(currentCluster);


			// Find the next potential cluster
			nextCluster = currentCluster + 1;

			while (findStartSector(nextCluster) < totalSectors && findNextCluster(nextCluster) != 0x00000000)
				nextCluster++;

		}
		else
			lastCluster = currentCluster;

		currentCluster = nextCluster;
	}
}

Disk::Entry::Entry(uint32_t sector, uint32_t offset, BYTE buffer[]) {
	this->sector = sector;
	this->offset = offset;
	memcpy_s(this->buffer, 32, buffer, 32);
}
