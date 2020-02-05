#include "endtoendcheck.h"

#include <openssl/sha.h>
#include <cstdlib>

using namespace std;

unsigned char *getCheckSum(char *buffer, int buffer_size) {
	unsigned char *obuf = new unsigned char[20]; // TODO: do we need to malloc
	SHA1((const unsigned char *)buffer, buffer_size, obuf);	
	return obuf;
}

int getCheckSumSize() {
	return 20;
}

bool compareCheckSums(char checkSum1[], char checkSum2[]) {
	for (int i = 0; i < 20; i++) 
		if (checkSum1[i] != checkSum2[i])
			return false;

	return true;
}