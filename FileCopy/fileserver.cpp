
#include "c150nastyfile.h"       // for c150nastyfile & framework
#include "c150dgmsocket.h"
#include "c150grading.h"
#include "c150nastydgmsocket.h"
#include "c150debug.h"


#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>               // for errno string formatting
#include <cerrno>
#include <cstring>               // for strerro
#include <iostream>              // for cout
#include <fstream>               // for input files
#include <openssl/sha.h>
#include <cstdlib>
#include <string>
#include <stdio.h>
#include <stdint.h>
#include <unordered_map>
#define PACKET_QUEUE 4


using namespace C150NETWORK;  // for all the comp150 utilities 

void checkDirectory(char *dirname);
string makeFileName(string dir, string name);

unsigned char *getCheckSum(char *buffer, int buffer_size) {
	unsigned char *obuf = new unsigned char[20];
	SHA1((const unsigned char *)buffer, buffer_size, obuf);	
	return obuf;
}

void addTMP(string * fileName) {
    string tempName = *fileName + ".TMP";
    // const char * oldName = (*fileName).c_str();
    // const char * newName = tempName.c_str();
    // cout << "Old name " << oldName << endl;
    // cout << "New Name " << newName << endl;
    if (rename((*fileName).c_str(), tempName.c_str()) != 0) {
        cerr << "Error renaming file " << fileName << 
                "  errno=" << strerror(errno) << endl;
        exit(16);
    }
    *fileName = tempName;
}

void removeTMP(string * fileName, string oldName) {
    if (rename((*fileName).c_str(), oldName.c_str()) != 0) {
        cerr << "Error renaming file " << fileName << 
                "  errno=" << strerror(errno) << endl;
        exit(16);
    }
    *fileName = oldName;
}

int main(int argc, char *argv[]) {
    GRADEME(argc, argv);	
	//
    // Variable declarations
    //
    ssize_t readlen;             // amount of data read from socket
    char incomingCheckSum[20]; // may wanna make static
    char incomingFileName[228];   // received message data
    char incomingConfirm[512];
    char buffer[512];
    char * big_buffer;
    int fileNastiness, networkNastiness;               // how aggressively do we drop packets, etc?
    struct stat statbuf;
    void *fopenretval;
    int incomingFileSize;
    int segment_num;
    int filelen;
    DIR *TARGET;                // Unix descriptor for target
    string targetName;
    // int size_so_far = 0;
    //
    // Check command line and parse arguments
    //
    unordered_map<int, bool> segment_exist;
    unordered_map<string, bool> file_done;
    if (argc != 4)  {
        fprintf(stderr,"Correct syntxt is: %s <networknastiness> <filenastiness> <targetdir>\n", argv[0]);
        exit(1);
    }
    if (strspn(argv[1], "0123456789") != strlen(argv[1])) {
        fprintf(stderr,"Network nastiness %s is not numeric\n", argv[1]);     
        fprintf(stderr,"Correct syntxt is: %s <nastiness_number>\n", argv[0]);     
        exit(4);
    }

    if (strspn(argv[2], "0123456789") != strlen(argv[2])) {
        fprintf(stderr,"File Nastiness %s is not numeric\n", argv[2]);     
        fprintf(stderr,"Correct syntxt is: %s <nastiness_number>\n", argv[0]);     
        exit(4);
    }
    TARGET = opendir(argv[3]);
    if (TARGET == NULL)
        mkdir(argv[3], 0700);
    closedir(TARGET);
    checkDirectory(argv[3]);

    networkNastiness = atoi(argv[1]);   // convert command line string to integer
    fileNastiness = atoi(argv[2]);   // convert command line string to integer

    //
    // Create socket, loop receiving and responding
    //
    try {
        C150DgmSocket *sock = new C150NastyDgmSocket(networkNastiness);
        while(1) {
            // Read packet
            readlen = sock -> read(buffer, 512);
            if (readlen == 0) {
                c150debug->printf(C150APPLICATION,"Read zero length message, trying again");
                continue;
            }
            memcpy(&incomingFileSize, buffer+256, 4);
            memcpy(&segment_num, buffer+260, 4);
            char ack[4];
            memcpy(ack, &segment_num, 4);
            sock->write(ack, 4);

            for (size_t i = 0; i < 20; i++)
                incomingCheckSum[i] = buffer[264+i];
            for (size_t i = 0; i < 227; i++)
                incomingFileName[i] = buffer[284+i];
            //
            // Clean up the message in case it contained junk
            //
            string incoming(incomingFileName); // Convert to C++ string ...it's slightly
                                              // easier to work with, and cleanString
                                              // expects it
            cleanString(incoming);  // c150ids-supplied utility: changes
                                    // non-printing characters to .
                                                //

            *GRADING << "File: " << incoming << " starting to receive file" << endl; 
           	targetName = makeFileName(argv[3], incoming);
            NASTYFILE curr_file(fileNastiness);
            fopenretval = curr_file.fopen(targetName.c_str(), "ab");  
                                                 // wraps Unix fopen
                                                // Note wb gives "write, binary"
                                                // which avoids line and munging

            if (lstat(targetName.c_str(), &statbuf) != 0) {
	            fprintf(stderr,"copyFile: Error stating supplied source file %s\n", targetName.c_str());
	            exit(20);
        	}

            //
            // Read a file
            //            
            int segment_size = 256;
            if (((segment_num + 1) * 256) > incomingFileSize)
                segment_size = incomingFileSize - segment_num * 256;
            if (segment_exist.find(segment_num) == segment_exist.end() &&
                (file_done.find(targetName) == file_done.end() or !file_done[targetName])) {
                *GRADING << "File: " << incoming << " writing segment number" << segment_num << endl; 
                filelen = curr_file.fwrite(buffer, 1, segment_size);
                segment_exist[segment_num] = true;
            }

            if (filelen != segment_size) {
                cerr << "Error writing file " << targetName << 
                    "  errno=" << strerror(errno) << endl;
                exit(16);
            }
	        if (curr_file.fclose() != 0 ) {
	            cerr << "Error closing input file " << targetName << 
	                    " errno=" << strerror(errno) << endl;
	            exit(16);
	        }

            if (lstat(targetName.c_str(), &statbuf) != 0) {
	            fprintf(stderr,"copyFile: Error stating supplied source file %s\n", targetName.c_str());
	            exit(20);
        	}

	        size_t targetSize = statbuf.st_size;
            if ((int)targetSize == incomingFileSize) {
                *GRADING << "File: " << incoming << " received, beginning end-to-end check" << endl; 
                    big_buffer = (char *)malloc(targetSize);
                    NASTYFILE outputFile(fileNastiness); 

                    // do an fopen on the input file
                    fopenretval = outputFile.fopen(targetName.c_str(), "rb");  
                                                        // wraps Unix fopen
                                                        // Note rb gives "read, binary"
                                                        // which avoids line end munging

                    if (fopenretval == NULL) {
                        cerr << "Error opening input file " << targetName << 
                                " errno=" << strerror(errno) << endl;
                        exit(12);
                    }

                    // 
                    // Read the whole file
                    //
                    filelen = outputFile.fread(big_buffer, 1, targetSize);
                    if (filelen != (int)targetSize) {
                        cerr << "Error reading file " << targetName << 
                                "  errno=" << strerror(errno) << endl;
                        exit(16);
                    }
                    if (outputFile.fclose() != 0 ) {
                        cerr << "Error closing input file " << targetName << 
                                " errno=" << strerror(errno) << endl;
                        exit(16);
                    }
                    unsigned char *checkSum;
                    checkSum = getCheckSum(big_buffer, targetSize);

                    string oldName = targetName;
                    addTMP(&targetName);

                    if (memcmp(checkSum, incomingCheckSum, sizeof(unsigned char) * 20) == 0) {
                        cout << "success" << endl;
                        file_done[targetName] = true;
                        sock->write("success", 7);
                    }
                    else {
                        *GRADING << "File: " << incoming << " end-to-end check succeded" << endl; 
                        cout << "check sum doesn't match, telling client to try again" << endl;
                        sock->write("failure", 7);
                    }
                    readlen = sock -> read(incomingConfirm, sizeof(incomingConfirm));
                    if (strcmp(incomingConfirm, "confirmedSuccess") == 0) {
                        *GRADING << "File: " << incoming << " end-to-end check succeded" << endl; 
                        removeTMP(&targetName, oldName);
                        cout << "File " << targetName << " copied successfully" << endl;
                    } else if (strcmp(incomingConfirm, "confirmedFailure") == 0) {
                        if (remove(targetName.c_str()) == 0) 
                            cout << "End to end failed, deleting temp file" << endl;
                        else {
                            cerr << "Error deleting file " << targetName << 
                                " errno=" << strerror(errno) << endl;
                            exit(16);
                        }
                        file_done[targetName] = false;
                    }
                    free(big_buffer);
                    segment_exist.clear();
            } 
        }
    }


    catch (C150NetworkException& e) {
        // Write to debug log
        c150debug->printf(C150ALWAYSLOG,"Caught C150NetworkException: %s\n",
                          e.formattedExplanation().c_str());
        // In case we're logging to a file, write to the console too
        cerr << argv[0] << ": caught C150NetworkException: " 
             << e.formattedExplanation() << endl;
    }

    // This only executes if there was an error caught above
    return 4;
}


// ------------------------------------------------------
//
//                   makeFileName
//
// Put together a directory and a file name, making
// sure there's a / in between
//
// ------------------------------------------------------

string makeFileName(string dir, string name) {
	return dir + "/" + name;  
}

void
checkDirectory(char *dirname) {
  struct stat statbuf;  
  if (lstat(dirname, &statbuf) != 0) {
    fprintf(stderr,"Error stating supplied source directory %s\n", dirname);
    exit(8);
  }

  if (!S_ISDIR(statbuf.st_mode)) {
    fprintf(stderr,"File %s exists but is not a directory\n", dirname);
    exit(8);
  }
}

