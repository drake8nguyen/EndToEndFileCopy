#include <string>

using namespace std;

unsigned char *getCheckSum(char *buffer, int buffer_size);
int getCheckSumSize();
bool compareCheckSums(char checkSum1[], char checkSum2[]);
