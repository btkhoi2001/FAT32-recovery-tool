#include <iostream>
#include <iomanip>
#include <Windows.h>
#include <fileapi.h>
#include "disk.h"
using namespace std;

int main() {
	char diskLetter;
	Disk disk;

	cout << "Remember to run this program as administrator or it won't work" << endl;
	cout << "Enter disk letter: ";
	cin >> diskLetter;

	if (disk.initilizeDiskHandle(diskLetter)) {
		cout << "Initilize successfully" << endl;
		cout << "..." << endl;

		disk.recoverFiles();

		cout << "Finish recovery";
	}
	else
		cout << "Initilize falied" << endl;

	return 0;
}